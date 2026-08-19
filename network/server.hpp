#pragma once

#include <cstdint>
#include <liburing.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "threadPool.hpp"
#include <iostream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <atomic>
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <queue>

#include <sys/eventfd.h>

#include "service/MessageParser.hpp"
#include "storage/DatabaseQueryer.hpp"
#include "service/SessionState.hpp"

namespace chatroom {

constexpr int QUEUE_SIZE = 2048;
constexpr int BUFFER_SIZE = 4096;
constexpr int HEARTBEAT_INTERVAL = 30;
constexpr int HEARTBEAT_TIMEOUT = 90;
constexpr uint64_t ACCEPT_TAG = 0xFFFFFFFFFFFFFFFEULL;
constexpr uint64_t TIMEOUT_TAG = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint64_t WAKEUP_TAG  = 0xFFFFFFFFFFFFFFFDULL;

class Server;
class MessageDispatcher;

struct Connection {
    int fd;
    char buffer[BUFFER_SIZE];
    std::vector<std::string> send_queue;
    bool sending = false;
    std::chrono::steady_clock::time_point last_active;

    std::string recv_buffer;
    uint32_t expected_length = 0;
    bool reading_header = true;

    uint64_t user_id = 0;
    std::string username;
    SessionState state = SessionState::NOT_LOGIN;
    std::string session_token;
    std::chrono::steady_clock::time_point login_time;

    std::unique_ptr<DatabaseQueryer> db_queryer_;
    std::unique_ptr<MessageDispatcher> dispatcher_;

    explicit Connection(int f) : fd(f), last_active(std::chrono::steady_clock::now()) {
        memset(buffer, 0, BUFFER_SIZE);
    }
    ~Connection();

    void touch() {
        last_active = std::chrono::steady_clock::now();
    }

    bool is_timeout() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_active);
        return elapsed.count() > HEARTBEAT_TIMEOUT;
    }

    bool is_logged_in() const {
        return state == SessionState::LOGGED_IN;
    }

    void login(uint64_t uid, const std::string& uname, const std::string& token) {
        user_id = uid;
        username = uname;
        session_token = token;
        state = SessionState::LOGGED_IN;
        login_time = std::chrono::steady_clock::now();
    }

    void logout() {
        user_id = 0;
        username.clear();
        session_token.clear();
        state = SessionState::NOT_LOGIN;
    }

    void init_business(std::shared_ptr<StorageManager> storage, Server* server);
};

}

#include "service/MessageDispatcher.hpp"

namespace chatroom {

class Server {
public:
    explicit Server(size_t thread_num = 4);
    ~Server();

    void set_on_connect(std::function<void(Connection*)> cb) { on_connect_ = std::move(cb); }
    void set_on_recv(std::function<void(Connection*, const std::string&)> cb) { on_recv_ = std::move(cb); }
    void set_on_send(std::function<void(Connection*, int)> cb) { on_send_ = std::move(cb); }
    void set_on_close(std::function<void(Connection*)> cb) { on_close_ = std::move(cb); }
    void set_storage(std::shared_ptr<StorageManager> s) { storage_ = std::move(s); }
    void set_verification_sender(std::shared_ptr<VerificationSender> s) { verification_sender_ = std::move(s); }
    std::shared_ptr<VerificationSender> verification_sender() const { return verification_sender_; }

    bool start(const std::string& host, int port);
    void run();
    void stop();

    void send_to(Connection* conn, const std::string& data);
    void send_to_async(int fd, const std::string& data);
    void wakeup();
    void close_connection(Connection* conn);
    void close_connection(int fd);

    size_t connection_count() const { return conns_.size(); }
    uint64_t total_connections() const { return conn_count_; }

    int get_fd_by_user_id(uint64_t user_id) const {
        std::lock_guard<std::mutex> lock(user_map_mutex_);
        auto it = user_to_fd_.find(user_id);
        return (it != user_to_fd_.end()) ? it->second : -1;
    }

    void register_user_fd(uint64_t user_id, int fd) {
        std::lock_guard<std::mutex> lock(user_map_mutex_);
        user_to_fd_[user_id] = fd;
    }

    void unregister_user_fd(uint64_t user_id) {
        std::lock_guard<std::mutex> lock(user_map_mutex_);
        user_to_fd_.erase(user_id);
    }

private:
    bool init_uring();
    bool init_socket(const std::string& host, int port);

    void submit_accept();
    void submit_recv(Connection* conn);
    void submit_send(Connection* conn);
    void submit_timeout();
    void submit_wakeup();

    void handle_cqe(struct io_uring_cqe* cqe);
    void handle_accept(int fd);
    void handle_recv(Connection* conn, int bytes);
    void handle_send(Connection* conn, int result);
    void handle_timeout();

    void cleanup_timeout_connections();
    void flush_pending_sends();

    struct io_uring ring_;
    int listen_fd_ = -1;
    int wakeup_fd_ = -1;

    using ConnectionPtr = std::shared_ptr<Connection>;
    std::unordered_map<int, ConnectionPtr> conns_;

    std::unordered_map<uint64_t, int> user_to_fd_;
    mutable std::mutex user_map_mutex_;

    std::atomic<bool> running_{true};
    std::atomic<bool> stopping_{false};
    uint64_t conn_count_ = 0;

    std::unique_ptr<ThreadPool> thread_pool_;

    struct PendingSend {
        int fd;
        std::string data;
    };

    std::queue<PendingSend> pending_sends_;
    std::mutex send_mutex_;

    std::function<void(Connection*)> on_connect_;
    std::function<void(Connection*, const std::string&)> on_recv_;
    std::function<void(Connection*, int)> on_send_;
    std::function<void(Connection*)> on_close_;

    std::shared_ptr<StorageManager> storage_;
    std::shared_ptr<VerificationSender> verification_sender_;
};

}