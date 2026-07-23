#include "server.hpp"
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
    // 1. 注册信号
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 2. 创建存储层（MySQL + Redis）
    auto storage = std::make_shared<StorageManager>();

    Server server(4);// 数值可调整：线程数量
    g_server = &server;
    server.set_storage(storage);

    server.set_on_connect([](Connection* conn) {
        std::cout << "[Callback] 新连接 fd=" << conn->fd << std::endl;
    });

    server.set_on_recv([&](Connection* conn, const std::string& data) {
        Message msg;
        if (!MessageParser::parse(data.data(), static_cast<int>(data.size()), msg)) {
            server.send_to_async(conn->fd, "消息解析失败");
            return;
        }

        std::cout << "[Pipeline] 收到消息: " << message_type_name(msg.type)
                  << " 来自 fd=" << conn->fd << std::endl;

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
    });

    server.set_on_send([](Connection* conn, int result) {
    });

    server.set_on_close([](Connection* conn) {
        std::cout << "[Callback] 连接 fd=" << conn->fd
                  << " 已关闭 (user=" << conn->username << ")" << std::endl;
    });

    // 5. 启动服务器
    if (!server.start(8080)) {
        std::cerr << "服务器启动失败" << std::endl;
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  ChatRoom 服务器已启动" << std::endl;
    std::cout << "  端口: 8080" << std::endl;
    std::cout << "  按 Ctrl+C 停止" << std::endl;
    std::cout << "========================================" << std::endl;

    // 6. 运行事件循环
    server.run();

    std::cout << "[Main] 服务器已退出" << std::endl;
    return 0;
}
