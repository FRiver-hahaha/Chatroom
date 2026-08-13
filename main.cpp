#include "network/server.hpp"
#include <glog/logging.h>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <sys/stat.h>

#include "config/Config.hpp"

using namespace chatroom;

Server* g_server = nullptr;

static void compact_log_prefix(std::ostream& os, const google::LogMessage& msg, void*) {
    const char* sev = google::GetLogSeverityName(msg.severity());
    os << '[' << (sev && sev[0] ? sev[0] : '?') << ' '
       << msg.basename() << ':' << msg.line() << "] ";
}

void signal_handler(int sig) {
    LOG(INFO) << "\n[Main] 收到信号 " << sig << "，正在关闭...";
    if (g_server) {
        g_server->stop();
    }
}

static void handle_session_events(Server& server,
                                  std::shared_ptr<StorageManager> storage,
                                  Connection* conn,
                                  const Message& msg,
                                  const QueryResult& result) {
    if (msg.type == MessageType::LOGIN_REQ && result.success) {
        server.register_user_fd(conn->user_id, conn->fd);
        auto pending = storage->get_pending_transfers(result.user_id);
        for (auto& t : pending) {
            chatroom::ChatMessage notify;
            notify.set_type(static_cast<uint32_t>(MessageType::FILE_TRANSFER_NOTIFY));
            notify.set_sender_id(t.sender_id);
            notify.set_target_id(result.user_id);
            auto* nb = notify.mutable_file_transfer_notify();
            nb->set_transfer_id(t.transfer_id);
            nb->set_sender_id(t.sender_id);
            auto sender_info = storage->get_user_by_id(t.sender_id);
            nb->set_sender_name(sender_info.success ? sender_info.username : std::to_string(t.sender_id));
            nb->set_file_name(t.file_name);
            nb->set_file_size(t.file_size);
            nb->set_total_chunks(t.total_chunks);
            if (!t.file_hash.empty())
                nb->set_file_hash(t.file_hash);
            std::string s;
            notify.SerializeToString(&s);
            server.send_to_async(conn->fd, s);
        }
    }
    if (msg.type == MessageType::LOGOUT_REQ || msg.type == MessageType::DELETE_ACCOUNT_REQ) {
        if (msg.sender_id != 0) server.unregister_user_fd(msg.sender_id);
    }
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();
    google::InstallPrefixFormatter(compact_log_prefix);
    FLAGS_log_dir = "logs";
    FLAGS_alsologtostderr = true;
    FLAGS_stderrthreshold = 1;
    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        FLAGS_logtostderr = true;
    }
    FLAGS_colorlogtostderr = true;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string bind_host = "0.0.0.0";
    int port = 8080;

    chatroom::Config config;
    if (config.load("chatroom.conf")) {
        bind_host = config.get("server", "host", bind_host);
        port = config.getInt("server", "port", port);
    }

    if (argc >= 3) bind_host = argv[1];
    if (argc >= 2) port = std::atoi(argv[argc >= 3 ? 2 : 1]);
    if (port <= 0 || port > 65535) {
        LOG(ERROR) << "[Main] 无效端口: " << port;
        return 1;
    }

    auto storage = std::make_shared<StorageManager>();
    if (!storage->connect("localhost", "chatroom", "Chatroom@2026#Secure", "chatroom")) {
        LOG(ERROR) << "[Main] 数据库连接失败，将以 mock 模式运行";
    }

    Server server(4);
    g_server = &server;
    server.set_storage(storage);

    server.set_on_connect([](Connection* conn) {
        LOG(INFO) << "[Callback] 新连接 fd=" << conn->fd;
    });

    server.set_on_recv([&](Connection* conn, const std::string& data) {
        Message msg;
        if (!MessageParser::parse(data.data(), static_cast<int>(data.size()), msg)) {
            LOG(ERROR) << "[Pipeline] 消息解析失败";
            server.send_to_async(conn->fd, "消息解析失败");
            return;
        }

        LOG(INFO) << "[Pipeline] 收到消息: " << message_type_name(msg.type)
                  << " 来自 fd=" << conn->fd
                  << " user=" << conn->username;

        if (!conn->db_queryer_) {
            server.send_to_async(conn->fd, "服务未就绪");
            return;
        }

        auto result = conn->db_queryer_->query(conn->state, msg);

        if (!conn->dispatcher_) {
            server.send_to_async(conn->fd, "服务未就绪");
            return;
        }

        conn->dispatcher_->dispatch(conn, msg, result);
        handle_session_events(server, storage, conn, msg, result);
    });

    server.set_on_send([](Connection* conn, int result) {
    });

    server.set_on_close([&](Connection* conn) {
        LOG(INFO) << "[Callback] 连接 fd=" << conn->fd
                  << " 已关闭 (user=" << conn->username << ")";
        if (conn->user_id != 0) {
            storage->set_offline(conn->user_id);
        }
    });

    if (!server.start(bind_host, port)) {
        LOG(ERROR) << "服务器启动失败";
        return 1;
    }

    LOG(INFO) << "========================================";
    LOG(INFO) << "  ChatRoom 服务器已启动";
    LOG(INFO) << "  监听地址: " << bind_host << ":" << port;
    LOG(INFO) << "  按 Ctrl+C 停止";
    LOG(INFO) << "========================================";

    server.run();

    LOG(INFO) << "[Main] 服务器已正常退出";
    return 0;
}