#include "server.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>

namespace chatroom {

Server::Server(size_t thread_num)
    : thread_pool_(std::make_unique<ThreadPool>(thread_num)) {}

Server::~Server() {
    stop();
    if (listen_fd_ > 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
}

bool Server::start(const std::string& host, int port) {
    if (!init_uring()) {
        std::cerr << "[Server] Failed to init io_uring" << std::endl;
        return false;
    }
    if (!init_socket(host, port)) {
        std::cerr << "[Server] Failed to init listen socket" << std::endl;
        return false;
    }
    submit_accept();
    submit_timeout();

    std::cout << "[Server] Started on " << host << ":" << port << std::endl;
    return true;
}

void Server::run() {
    while (running_.load()) {
        struct io_uring_cqe* cqe;

        int ret = io_uring_wait_cqe(&ring_, &cqe);
        if (ret < 0) {
            std::cerr << "[Server] wait_cqe error: " << -ret << std::endl;
            continue;
        }

        handle_cqe(cqe);
        io_uring_cqe_seen(&ring_, cqe);

        while (true) {
            struct io_uring_cqe* more;
            ret = io_uring_peek_cqe(&ring_, &more);
            if (ret == -EAGAIN) break;
            if (ret < 0) break;

            handle_cqe(more);
            io_uring_cqe_seen(&ring_, more);
        }
        flush_pending_sends();

        io_uring_submit(&ring_);
    }
}

void Server::stop() {
    running_.store(false);
    io_uring_queue_exit(&ring_);

    for (auto& [fd, conn] : conns_) {
        if (conn->user_id != 0 && on_close_) {
            on_close_(conn.get());
        }
        close(fd);
    }
    conns_.clear();
    std::cout << "[Server] Stopped" << std::endl;
}

void Server::send_to(Connection* conn, const std::string& data) {
    if (!conn) return;
    
    uint32_t len = htonl(data.size());
    std::string packed(4, '\0');
    memcpy(&packed[0], &len, 4);
    packed.append(data);
    
    conn->send_queue.push_back(packed);
    submit_send(conn);
}

void Server::send_to_async(int fd, const std::string& data) {
    uint32_t len = htonl(data.size());
    std::string packed(4, '\0');
    memcpy(&packed[0], &len, 4);
    packed.append(data);
    
    std::lock_guard<std::mutex> lock(send_mutex_);
    pending_sends_.push({fd, packed});

    // 唤醒 io_uring 主循环，立即刷新待发送队列
    wakeup();
}

void Server::wakeup() {
    if (wakeup_fd_ >= 0) {
        uint64_t val = 1;
        ::write(wakeup_fd_, &val, sizeof(val));
    }
}

void Server::flush_pending_sends() {
    std::lock_guard<std::mutex> lock(send_mutex_);
    while (!pending_sends_.empty()) {
        auto& send = pending_sends_.front();
        auto it = conns_.find(send.fd);
        if (it != conns_.end()) {
            it->second->send_queue.push_back(send.data);
            submit_send(it->second.get());
        }
        pending_sends_.pop();
    }
}

void Server::close_connection(Connection* conn) {
    if (!conn) return;
    close_connection(conn->fd);
}

void Server::close_connection(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;

    Connection* conn = it->second.get();

    if (conn->user_id != 0) {
        unregister_user_fd(conn->user_id);
    }

    if (on_close_) {
        on_close_(conn);
    }

    close(fd);
    conns_.erase(it);

    std::cout << "[Server] Connection " << fd << " closed, remaining: "
              << conns_.size() << std::endl;
}

bool Server::init_uring() {
    struct io_uring_params params = {};
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 1000;

    int ret = io_uring_queue_init_params(QUEUE_SIZE, &ring_, &params);
    if (ret < 0) {
        std::cerr << "[Server] io_uring init failed: " << -ret << std::endl;
        return false;
    }

    std::cout << "[Server] io_uring ready: SQ=" << *ring_.sq.kring_entries
              << ", CQ=" << *ring_.cq.kring_entries << ", SQPOLL=on" << std::endl;

    // 创建 eventfd 用于唤醒 io_uring 主循环
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    if (wakeup_fd_ < 0) {
        std::cerr << "[Server] eventfd creation failed: " << strerror(errno) << std::endl;
        return false;
    }
    submit_wakeup();

    return true;
}

bool Server::init_socket(const std::string& host, int port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        perror("socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd_);
        return false;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    if (!host.empty() && host != "0.0.0.0" && inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[Server] Invalid bind address '" << host << "', falling back to 0.0.0.0" << std::endl;
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    addr.sin_port = htons(port);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd_);
        return false;
    }

    if (listen(listen_fd_, 128) < 0) {
        perror("listen");
        close(listen_fd_);
        return false;
    }

    char bind_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, bind_str, sizeof(bind_str));
    std::cout << "[Server] Listening on " << bind_str << ":" << port << std::endl;
    return true;
}

void Server::submit_accept() {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (!sqe) return;
    }
    io_uring_prep_multishot_accept(sqe, listen_fd_, nullptr, nullptr, 0);
    io_uring_sqe_set_data64(sqe, ACCEPT_TAG);
    io_uring_submit(&ring_);
}

void Server::submit_recv(Connection* conn) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;

    io_uring_prep_recv(sqe, conn->fd, conn->buffer, BUFFER_SIZE, 0);
    io_uring_sqe_set_data64(sqe, (uint64_t)conn->fd);
}

void Server::submit_send(Connection* conn) {
    if (conn->sending || conn->send_queue.empty()) return;

    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;

    std::string& data = conn->send_queue.front();
    io_uring_prep_send(sqe, conn->fd, data.data(), data.size(), 0);
    io_uring_sqe_set_data64(sqe, (uint64_t)conn->fd);
    conn->sending = true;
}

void Server::submit_timeout() {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;

    struct __kernel_timespec ts = {HEARTBEAT_INTERVAL, 0};
    io_uring_prep_timeout(sqe, &ts, 0, 0);
    io_uring_sqe_set_data64(sqe, TIMEOUT_TAG);
}

void Server::submit_wakeup() {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;

    static uint64_t dummy;
    io_uring_prep_read(sqe, wakeup_fd_, &dummy, sizeof(dummy), 0);
    io_uring_sqe_set_data64(sqe, WAKEUP_TAG);
}

void Server::handle_cqe(struct io_uring_cqe* cqe) {
    uint64_t tag = io_uring_cqe_get_data64(cqe);
    int res = cqe->res;

    if (tag == TIMEOUT_TAG) {
        handle_timeout();
        return;
    }

    if (tag == WAKEUP_TAG) {
        submit_wakeup();  // 重新注册，等待下次唤醒
        return;
    }

    if (tag == ACCEPT_TAG) {
        if (res < 0) {
            submit_accept();
            return;
        }
        handle_accept(res);
        return;
    }

    auto it = conns_.find((int)tag);
    if (it == conns_.end()) return;

    Connection* conn = it->second.get();
    conn->touch();

    if (conn->sending) {
        handle_send(conn, res);
    } else if (res > 0) {
        handle_recv(conn, res);
    } else {
        close_connection(conn->fd);
    }
}

void Server::handle_accept(int fd) {
    std::cout << "[Server] New client: fd=" << fd << std::endl;

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return;
    }

    auto conn = std::make_shared<Connection>(fd);
    conns_[fd] = conn;
    conn_count_++;

    if (storage_) {
        conn->init_business(storage_, this);
    }

    if (on_connect_) {
        on_connect_(conn.get());
    }

    submit_recv(conn.get());
}

void Server::handle_recv(Connection* conn, int bytes) {
    std::cout << "[Server] Recv " << bytes << " bytes from fd " << conn->fd << std::endl;

    // 追加到接收缓冲区
    conn->recv_buffer.append(conn->buffer, bytes);

    // 重新提交 recv
    submit_recv(conn);

    // 循环解析完整的消息帧
    while (true) {
        if (conn->reading_header) {
            if (conn->recv_buffer.size() < 4) break;
            
            uint32_t net_len;
            memcpy(&net_len, conn->recv_buffer.data(), 4);
            conn->expected_length = ntohl(net_len);
            conn->recv_buffer.erase(0, 4);
            conn->reading_header = false;
        }

        if (!conn->reading_header) {
            if (conn->recv_buffer.size() < conn->expected_length) break;

            std::string message_data = conn->recv_buffer.substr(0, conn->expected_length);
            conn->recv_buffer.erase(0, conn->expected_length);
            conn->reading_header = true;
            conn->expected_length = 0;

            auto it = conns_.find(conn->fd);
            if (it != conns_.end()) {
                auto conn_shared = it->second;
                int fd = conn->fd;

                thread_pool_->submit(
                    [conn_shared, data_str = std::move(message_data), fd, this]() {
                        std::cout << "[线程池] 处理 " << data_str.size() << " 字节来自 fd " << fd << std::endl;
                        if (on_recv_) {
                            on_recv_(conn_shared.get(), data_str);
                        }
                    },
                    []() {}
                );
            }
        }
    }
}

void Server::handle_send(Connection* conn, int result) {
    conn->sending = false;

    if (result < 0) {
        close_connection(conn->fd);
        return;
    }

    if (!conn->send_queue.empty()) {
        conn->send_queue.erase(conn->send_queue.begin());
    }

    if (on_send_) {
        on_send_(conn, result);
    }

    if (!conn->send_queue.empty()) {
        submit_send(conn);
    }
}

void Server::handle_timeout() {
    cleanup_timeout_connections();
    if (running_.load()) {
        submit_timeout();
    }
}

void Server::cleanup_timeout_connections() {
    std::vector<int> to_remove;
    for (auto& [fd, conn] : conns_) {
        if (conn->is_timeout()) {
            to_remove.push_back(fd);
        }
    }

    for (int fd : to_remove) {
        std::cout << "[Server] Connection " << fd << " timed out" << std::endl;
        close_connection(fd);
    }
}

Connection::~Connection() = default;

void Connection::init_business(std::shared_ptr<StorageManager> storage, Server* server) {
    db_queryer_ = std::make_unique<DatabaseQueryer>(storage);
    dispatcher_ = std::make_unique<MessageDispatcher>(
        [server](int fd, const std::string& data) {
            server->send_to_async(fd, data);
        },
        [server](uint64_t user_id) -> int {
            return server->get_fd_by_user_id(user_id);
        }
    );
}

} // namespace chatroom