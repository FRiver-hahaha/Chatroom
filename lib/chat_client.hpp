/**
 * ChatRoom 客户端网络库（纯 C++17，无 Qt 依赖）
 *
 * 从 client/client.cpp 提取，提供：
 *  - TCP socket 连接 + 4 字节大端长度前缀帧协议
 *  - Protobuf 消息的构建与解析
 *  - 后台接收线程 + 异步回调分发
 *  - 所有业务 API（账号、好友、群组、聊天、文件）
 */

#pragma once

#include "chatroom.pb.h"
#include "service/MessageType.hpp"

#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <cstdint>
#include <ctime>

namespace chatroom {

// ============================================================
// 回调类型定义
// ============================================================

/// 收到服务端响应时调用（msg.type 对应请求的响应类型）
using ResponseCallback = std::function<void(const ChatMessage&)>;

/// 收到服务端推送（通知、私聊/群聊消息等非请求-响应类的消息）
using NotificationCallback = std::function<void(const std::string&)>;

/// 日志输出回调（替代 std::cout / std::cerr）
using LogCallback = std::function<void(const std::string&, int level)>; // level: 0=info, 1=warn, 2=error

// ============================================================
// ChatClient — 聊天客户端网络层
// ============================================================

class ChatClient {
public:
    ChatClient();
    ~ChatClient();

    // ===== 连接管理 =====
    bool connect(const std::string& host, int port);
    void disconnect();
    bool is_connected() const;

    // ===== 回调注册 =====
    void set_notification_callback(NotificationCallback cb);
    void set_log_callback(LogCallback cb);

    // ===== 异步发送（带回调）=====
    /// 发送请求，收到对应响应类型后调用回调
    void send_request(const ChatMessage& msg, MessageType expected_response,
                      ResponseCallback on_response);

    // ===== 业务 API（异步版本）=====

    // 账号模块
    void login(const std::string& user, const std::string& pass, ResponseCallback cb);
    void register_user(const std::string& user, const std::string& pass,
                       const std::string& nickname, ResponseCallback cb);
    void logout(ResponseCallback cb);
    void delete_account(const std::string& password, ResponseCallback cb);

    // 好友模块
    void add_friend(uint64_t target_uid, ResponseCallback cb);
    void delete_friend(uint64_t target_uid, ResponseCallback cb);
    void block_friend(uint64_t target_uid, ResponseCallback cb);
    void unblock_friend(uint64_t target_uid, ResponseCallback cb);
    void query_friends(ResponseCallback cb);

    // 群组模块
    void create_group(const std::string& name, const std::string& desc,
                      bool is_public, ResponseCallback cb);
    void join_group(uint64_t gid, ResponseCallback cb);
    void quit_group(uint64_t gid, ResponseCallback cb);
    void query_group_list(ResponseCallback cb);
    void query_group_members(uint64_t gid, ResponseCallback cb);
    void add_group_admin(uint64_t gid, uint64_t target_uid, ResponseCallback cb);
    void remove_group_admin(uint64_t gid, uint64_t target_uid, ResponseCallback cb);
    void approve_join_group(uint64_t gid, uint64_t target_uid, ResponseCallback cb);
    void reject_join_group(uint64_t gid, uint64_t target_uid, ResponseCallback cb);
    void remove_group_member(uint64_t gid, uint64_t target_uid, ResponseCallback cb);

    // 聊天模块
    void send_private_chat(uint64_t target_uid, const std::string& text, ResponseCallback cb);
    void send_group_chat(uint64_t gid, const std::string& text, ResponseCallback cb);
    void get_history(uint64_t target_uid, uint64_t gid, int limit, ResponseCallback cb);

    // 文件模块
    void upload_file(const std::string& filepath, ResponseCallback cb);
    void download_file(uint64_t file_id, const std::string& save_path, ResponseCallback cb);

    // ===== 会话状态 =====
    bool is_logged_in() const;
    uint64_t user_id() const;
    const std::string& username() const;
    const std::string& token() const;
    const std::string& nickname() const;

    // ===== 缓存数据 =====
    const std::vector<chatroom::FriendInfo>& cached_friends() const;
    const std::vector<chatroom::GroupInfo>& cached_groups() const;

private:
    // ===== 内部网络层 =====
    bool send_raw(const std::string& data);
    bool send_message(const ChatMessage& msg);
    void recv_loop();
    void process_payload(std::string payload);

    // ===== 辅助 =====
    void log(const std::string& msg, int level = 0);

    // ===== 成员变量 =====

    // 连接
    int sockfd_ = -1;
    std::atomic<bool> running_{false};
    std::thread recv_thread_;

    // 会话
    uint64_t user_id_ = 0;
    std::string username_;
    std::string nickname_;
    std::string token_;

    // 异步回调分发（替代 wait_response + responses_ 队列）
    mutable std::mutex callback_mutex_;
    std::unordered_map<uint32_t, ResponseCallback> response_callbacks_;

    // 通知回调
    mutable std::mutex notify_mutex_;
    NotificationCallback notification_cb_;
    LogCallback log_cb_;

    // 缓存
    std::vector<chatroom::FriendInfo> friend_cache_;
    std::vector<chatroom::GroupInfo> group_cache_;
};

} // namespace chatroom
