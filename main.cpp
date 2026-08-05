#include "network/server.hpp"
#include <iostream>
#include <csignal>

using namespace chatroom;

Server* g_server = nullptr;

void signal_handler(int sig) {
    std::cout << "\n[Main] 收到信号 " << sig << "，正在关闭..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto storage = std::make_shared<StorageManager>();
    if (!storage->connect("localhost", "chatroom", "Chatroom@2026#Secure", "chatroom")) {
        std::cerr << "[Main] 数据库连接失败，将以 mock 模式运行" << std::endl;
    }

    Server server(4);
    g_server = &server;
    server.set_storage(storage);

    server.set_on_connect([](Connection* conn) {
        std::cout << "[Callback] 新连接 fd=" << conn->fd << std::endl;
    });

    server.set_on_recv([&](Connection* conn, const std::string& data) {
        Message msg;
        if (!MessageParser::parse(data.data(), static_cast<int>(data.size()), msg)) {
            std::cerr << "[Pipeline] 消息解析失败" << std::endl;
            server.send_to_async(conn->fd, "消息解析失败");
            return;
        }

        std::cout << "[Pipeline] 收到消息: " << message_type_name(msg.type)
                  << " 来自 fd=" << conn->fd 
                  << " user=" << conn->username << std::endl;

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

        if (msg.type == MessageType::LOGIN_REQ && result.success) {
            server.register_user_fd(conn->user_id, conn->fd);
            // 通知待处理的文件传输
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
            server.unregister_user_fd(conn->user_id);
        }
    });

    server.set_on_send([](Connection* conn, int result) {
    });

    server.set_on_close([&](Connection* conn) {
        std::cout << "[Callback] 连接 fd=" << conn->fd
                  << " 已关闭 (user=" << conn->username << ")" << std::endl;
        if (conn->user_id != 0) {
            storage->set_offline(conn->user_id);
        }
    });

    if (!server.start(8080)) {
        std::cerr << "服务器启动失败" << std::endl;
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  ChatRoom 服务器已启动" << std::endl;
    std::cout << "  端口: 8080" << std::endl;
    std::cout << "  按 Ctrl+C 停止" << std::endl;
    std::cout << "========================================" << std::endl;

    server.run();

    std::cout << "[Main] 服务器已退出" << std::endl;
    return 0;
}