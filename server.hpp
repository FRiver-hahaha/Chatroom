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

namespace chatroom {

constexpr int QUEUE_SIZE = 2048;
constexpr int BUFFER_SIZE = 4096;
constexpr int HEARTBEAT_INTERVAL = 30;
constexpr int HEARTBEAT_TIMEOUT = 90;
constexpr uint64_t ACCEPT_TAG = 0xFFFFFFFFFFFFFFFEULL;
constexpr uint64_t TIMEOUT_TAG = 0xFFFFFFFFFFFFFFFFULL;
struct Connection {
    int fd;
    char buffer[BUFFER_SIZE];
    std::vector<std::string> send_queue;
    bool sending = false;
    std::chrono::steady_clock::time_point last_active;

    explicit Connection(int f) : fd(f), last_active(std::chrono::steady_clock::now()) {
        memset(buffer, 0, BUFFER_SIZE);
    }

    void touch() {
        last_active = std::chrono::steady_clock::now();
    }

    bool is_timeout() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_active);
        return elapsed.count() > HEARTBEAT_TIMEOUT;
    }
};

using OnConnectCallback = std::function<void(Connection* conn)>;
using OnRecvCallback = std::function<void(Connection* conn, int bytes)>;
using OnSendCallback = std::function<void(Connection* conn, int result)>;
using OnCloseCallback = std::function<void(Connection* conn)>;

class Server {
public:
    explicit Server(size_t thread_num = 4);
    ~Server();

    void set_on_connect(OnConnectCallback cb) { on_connect_ = std::move(cb); }
    void set_on_recv(OnRecvCallback cb) { on_recv_ = std::move(cb); }
    void set_on_send(OnSendCallback cb) { on_send_ = std::move(cb); }
    void set_on_close(OnCloseCallback cb) { on_close_ = std::move(cb); }
    bool start(int port);
    void run();
    void stop();
    void send_to(Connection* conn, const std::string& data);
    void send_to_async(int fd, const std::string& data);
    void close_connection(Connection* conn);
    size_t connection_count() const { return conns_.size(); }
    uint64_t total_connections() const { return conn_count_; }

private:

    bool init_uring();
    bool init_socket(int port);
    void submit_accept();
    void submit_recv(Connection* conn);
    void submit_send(Connection* conn);
    void submit_timeout();

    void handle_cqe(struct io_uring_cqe* cqe);
    void handle_accept(int fd);
    void handle_recv(Connection* conn, int bytes);
    void handle_send(Connection* conn, int result);
    void handle_timeout();

    void close_connection(int fd);
    void cleanup_timeout_connections();

    void flush_pending_sends();

    struct io_uring ring_;
    int listen_fd_ = -1;

    using ConnectionPtr = std::shared_ptr<Connection>;
    std::unordered_map<int, ConnectionPtr> conns_;

    std::atomic<bool> running_{true};
    uint64_t conn_count_ = 0;

    std::unique_ptr<ThreadPool> thread_pool_;

    struct PendingSend {
        int fd;
        std::string data;
    };
    std::queue<PendingSend> pending_sends_;
    std::mutex send_mutex_;
    OnConnectCallback on_connect_;
    OnRecvCallback on_recv_;
    OnSendCallback on_send_;
    OnCloseCallback on_close_;
};

} // namespace chatroom