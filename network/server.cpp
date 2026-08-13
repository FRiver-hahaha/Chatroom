#include "server.hpp"
#include <glog/logging.h>
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
        LOG(ERROR) << "[Server] Failed to init io_uring";
        return false;
    }
    if (!init_socket(host, port)) {
        LOG(ERROR) << "[Server] Failed to init listen socket";
        return false;
    }
    submit_accept();
    submit_timeout();
    LOG(INFO) << "[Server] Started on " << host << ":" << port;
    return true;
}

void Server::stop() {
    if (stopping_.exchange(true)) {
        return;
    }
    LOG(INFO) << "[Server] 收到停止信号，正在退出主循环...";
    running_.store(false);
    if (wakeup_fd_ >= 0) {
        uint64_t val = 1;
        ::write(wakeup_fd_, &val, sizeof(val));
    }
}

void Server::run() {
    LOG(INFO) << "[Server] 主循环开始";
    while (running_.load()) {
        struct io_uring_cqe* cqe = nullptr;
        struct __kernel_timespec ts = {0, 100000000};
        int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);

        if (ret < 0) {
            if (ret == -ETIME) {
                continue;
            }
            if (ret == -EAGAIN) {
                struct timespec ts = {0, 1000000}; // 1ms
                nanosleep(&ts, nullptr);
                continue;
            }
            if (ret == -EINTR) {
                if (!running_.load()) {
                    LOG(INFO) << "[Server] 收到退出信号，停止主循环";
                    break;
                }
                continue;
            }
            LOG(ERROR) << "[Server] wait_cqe error: " << -ret;
            continue;
        }

        handle_cqe(cqe);
        io_uring_cqe_seen(&ring_, cqe);

        while (true) {
            struct io_uring_cqe* more = nullptr;
            ret = io_uring_peek_cqe(&ring_, &more);
            if (ret == -EAGAIN) break;
            if (ret < 0) break;
            handle_cqe(more);
            io_uring_cqe_seen(&ring_, more);
        }

        flush_pending_sends();
        io_uring_submit(&ring_);
    }

    LOG(INFO) << "[Server] 主循环退出，正在清理资源...";

    for (auto& [fd, conn] : conns_) {
        if (conn->user_id != 0 && on_close_) {
            on_close_(conn.get());
        }
        close(fd);
    }
    conns_.clear();
    LOG(INFO) << "[Server] 所有连接已关闭";

    io_uring_queue_exit(&ring_);
    LOG(INFO) << "[Server] io_uring 已清理";

    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }

    {
        std::lock_guard<std::mutex> lock(user_map_mutex_);
        user_to_fd_.clear();
    }

    LOG(INFO) << "[Server] 资源清理完成，服务器已停止";
}

void Server::send_to(Connection* conn, const std::string& data) {
    if (!conn) return;
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    std::string packed(4, '\0');
    memcpy(&packed[0], &len, 4);
    packed.append(data);
    conn->send_queue.push_back(packed);
    submit_send(conn);
}

void Server::send_to_async(int fd, const std::string& data) {
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    std::string packed(4, '\0');
    memcpy(&packed[0], &len, 4);
    packed.append(data);
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        pending_sends_.push({fd, std::move(packed)});
    }
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
            it->second->send_queue.push_back(std::move(send.data));
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
    LOG(INFO) << "[Server] Connection " << fd << " closed, remaining: " << conns_.size();
}

bool Server::init_uring() {
    struct io_uring_params params = {};
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 1000;

    int ret = io_uring_queue_init_params(QUEUE_SIZE, &ring_, &params);
    if (ret < 0) {
        LOG(ERROR) << "[Server] io_uring init failed: " << -ret;
        return false;
    }

    LOG(INFO) << "[Server] io_uring ready: SQ=" << *ring_.sq.kring_entries
              << ", CQ=" << *ring_.cq.kring_entries << ", SQPOLL=on";

    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    if (wakeup_fd_ < 0) {
        LOG(ERROR) << "[Server] eventfd creation failed: " << strerror(errno);
        return false;
    }
    submit_wakeup();
    return true;
}

bool Server::init_socket(const std::string& host, int port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        LOG(ERROR) << "socket: " << strerror(errno);
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG(ERROR) << "setsockopt: " << strerror(errno);
        close(listen_fd_);
        return false;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    if (!host.empty() && host != "0.0.0.0" && inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        LOG(ERROR) << "[Server] Invalid bind address '" << host << "', falling back to 0.0.0.0";
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    addr.sin_port = htons(port);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG(ERROR) << "bind: " << strerror(errno);
        close(listen_fd_);
        return false;
    }

    if (listen(listen_fd_, 128) < 0) {
        LOG(ERROR) << "listen: " << strerror(errno);
        close(listen_fd_);
        return false;
    }

    char bind_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, bind_str, sizeof(bind_str));
    LOG(INFO) << "[Server] Listening on " << bind_str << ":" << port;
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
    io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(conn->fd));
}

void Server::submit_send(Connection* conn) {
    if (conn->sending || conn->send_queue.empty()) return;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;
    std::string& data = conn->send_queue.front();
    io_uring_prep_send(sqe, conn->fd, data.data(), data.size(), 0);
    io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(conn->fd));
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
        submit_wakeup();
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

    int fd = static_cast<int>(tag);
    auto it = conns_.find(fd);
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
    LOG(INFO) << "[Server] New client: fd=" << fd;

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
    submit_accept();
}

void Server::handle_recv(Connection* conn, int bytes) {
    LOG(INFO) << "[Server] Recv " << bytes << " bytes from fd " << conn->fd;

    conn->recv_buffer.append(conn->buffer, bytes);
    submit_recv(conn);

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
        LOG(ERROR) << "[Server] Send error on fd " << conn->fd << ": " << -result;
        close_connection(conn->fd);
        return;
    }

    if (!conn->send_queue.empty()) {
        size_t sent = static_cast<size_t>(result);
        std::string& front = conn->send_queue.front();
        if (sent < front.size()) {
            front.erase(0, sent);
            submit_send(conn);
            return;
        }
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
        LOG(INFO) << "[Server] Connection " << fd << " timed out";
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

}