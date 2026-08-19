#pragma once

#include "MessageType.hpp"
#include "MessageParser.hpp"
#include "storage/DatabaseQueryResult.hpp"
#include <memory>
#include <iostream>
#include <functional>
#include <string>

namespace chatroom {

struct Connection;

class MessageDispatcher {
public:
    using SendFunc = std::function<void(int, const std::string&)>;// 包装发送目标和发送内容函数
    using LookupFunc = std::function<int(uint64_t user_id)>;// 包装查找当前连接和是否在线的函数

    explicit MessageDispatcher(SendFunc sender, LookupFunc lookup)
        : sender_(std::move(sender)), lookup_(std::move(lookup)) {}

    void dispatch(Connection* conn, const Message& msg, const QueryResult& result) {// 主分发函数
        if (!conn) { std::cerr << "[MessageDispatcher] Connection is null" << std::endl; return; }
        std::cout << "[MessageDispatcher] Dispatching: " << message_type_name(msg.type) << std::endl;

        int tv = static_cast<int>(msg.type);
        if (tv >= 1 && tv < 100)        dispatch_account(conn, msg, result);
        else if (tv >= 100 && tv < 200) dispatch_friend(conn, msg, result);
        else if (tv >= 200 && tv < 300) dispatch_group(conn, msg, result);
        else if (tv >= 300 && tv < 400) dispatch_chat(conn, msg, result);
        else if (tv >= 420 && tv < 440) dispatch_file_send(conn, msg, result);
        else std::cerr << "[MessageDispatcher] Unknown type: " << tv << std::endl;
    }

private:
    void send_rsp(Connection* conn, const Message& msg, const QueryResult& result) {// 发送响应消息
        sender_(conn->fd, MessageParser::serialize_response(msg, result));
    }

    void send_rsp_and_logout(Connection* conn, const Message& msg, const QueryResult& result) {// 发送响应消息，并且登出(删除账户)
        send_rsp(conn, msg, result);
        if (result.success) conn->logout();
    }

    void notify_target(uint64_t user_id, const std::string& text) {// 发送给用户
        int fd = lookup_(user_id);
        if (fd >= 0) sender_(fd, text);
    }

    void dispatch_account(Connection* conn, const Message& msg, const QueryResult& result) {// 账户模块分发
        switch (msg.type) {
            case MessageType::LOGIN_REQ:          handle_login(conn, msg, result); break;
            case MessageType::REGISTER_REQ:       send_rsp(conn, msg, result); break;
            case MessageType::VERIFY_CODE_REQ:    send_rsp(conn, msg, result); break;
            case MessageType::PASSWORD_RESET_REQ: send_rsp(conn, msg, result); break;
            case MessageType::LOGOUT_REQ:         handle_logout(conn, msg, result); break;
            case MessageType::DELETE_ACCOUNT_REQ: send_rsp_and_logout(conn, msg, result); break;
            case MessageType::UPDATE_PROFILE_REQ: send_rsp(conn, msg, result); break;
            case MessageType::HEARTBEAT_REQ:      send_rsp(conn, msg, result); break;
            default: break;
        }
    }

    void handle_login(Connection* conn, const Message& msg, const QueryResult& result) {// 登录
        send_rsp(conn, msg, result);
        if (!result.success) return;

        conn->user_id = result.user_id;
        conn->username = result.username;
        conn->state = SessionState::LOGGED_IN;
        conn->session_token = result.token;

        if (!result.offline_messages.empty()) {// 拉取离线消息
            sender_(conn->fd, "你有 " + std::to_string(result.offline_messages.size()) + " 条离线消息");
            for (const auto& m : result.offline_messages)
                sender_(conn->fd, "[离线消息][" + m.sender_name + "]: " + m.content);
        }
        for (const auto& f : result.friend_list) {
            if (f.is_online)
                notify_target(f.user_id, "[系统通知] 好友 " + conn->username + " 上线了");
        }
    }

    void handle_logout(Connection* conn, const Message& msg, const QueryResult& result) {// 登出
        std::string username = conn->username;
        send_rsp_and_logout(conn, msg, result);
        if (!result.success || username.empty()) return;
        for (const auto& f : result.friend_list) {
            if (f.is_online)
                notify_target(f.user_id, "[系统通知] 好友 " + username + " 下线了");
        }
    }

    void dispatch_friend(Connection* conn, const Message& msg, const QueryResult& result) {// 好友模块
        switch (msg.type) {
            case MessageType::ADD_FRIEND_REQ:     handle_add_friend(conn, msg, result); break;
            case MessageType::DELETE_FRIEND_REQ:  send_rsp(conn, msg, result); break;
            case MessageType::QUERY_FRIEND_REQ:   send_rsp(conn, msg, result); break;
            case MessageType::BLOCK_FRIEND_REQ:   send_rsp(conn, msg, result); break;
            case MessageType::UNBLOCK_FRIEND_REQ: send_rsp(conn, msg, result); break;
            case MessageType::QUERY_BLOCKED_REQ: send_rsp(conn, msg, result); break;
            default: break;
        }
    }

    void handle_add_friend(Connection* conn, const Message& msg, const QueryResult& result) {// 添加好友
        send_rsp(conn, msg, result);
        if (result.success && msg.target_id != 0)
            notify_target(msg.target_id, "[系统通知] 用户 " + conn->username + " 请求添加你为好友");
    }

    void dispatch_group(Connection* conn, const Message& msg, const QueryResult& result) {// 群组模块
        switch (msg.type) {
            case MessageType::CREATE_GROUP_REQ:          send_rsp(conn, msg, result); break;
            case MessageType::DISMISS_GROUP_REQ:         handle_dismiss_group(conn, msg, result); break;
            case MessageType::JOIN_GROUP_REQ:            handle_join_group(conn, msg, result); break;
            case MessageType::QUIT_GROUP_REQ:            handle_quit_group(conn, msg, result); break;
            case MessageType::QUERY_GROUP_LIST_REQ:      send_rsp(conn, msg, result); break;
            case MessageType::QUERY_GROUP_MEMBERS_REQ:   send_rsp(conn, msg, result); break;
            case MessageType::ADD_GROUP_ADMIN_REQ:       handle_group_change(conn, msg, result); break;
            case MessageType::REMOVE_GROUP_ADMIN_REQ:    handle_group_change(conn, msg, result); break;
            case MessageType::APPROVE_JOIN_GROUP_REQ:    handle_approve_join_group(conn, msg, result); break;
            case MessageType::REJECT_JOIN_GROUP_REQ:    handle_reject_join_group(conn, msg, result); break;
            case MessageType::REMOVE_GROUP_MEMBER_REQ:   handle_remove_group_member(conn, msg, result); break;
            default: break;
        }
    }

    void broadcast_group_change(uint64_t group_id, const QueryResult& result, uint64_t skip_user_id = 0) {// 向群内在线成员广播成员变更（跳过操作者，其响应已带最新成员）
        for (const auto& m : result.group_members) {
            if (m.user_id == skip_user_id) continue;
            int fd = lookup_(m.user_id);
            if (fd < 0) continue;
            ChatMessage notify;
            notify.set_type(static_cast<uint32_t>(MessageType::GROUP_MEMBERS_CHANGED));
            notify.set_group_id(group_id);
            notify.mutable_group_members_changed()->set_group_id(group_id);
            std::string s;
            notify.SerializeToString(&s);
            sender_(fd, s);
        }
    }

    void handle_group_change(Connection* conn, const Message& msg, const QueryResult& result) {// 通用群成员变更：响应 + 广播
        send_rsp(conn, msg, result);
        if (result.success && result.group_id != 0)
            broadcast_group_change(result.group_id, result, conn->user_id);
    }

    void handle_quit_group(Connection* conn, const Message& msg, const QueryResult& result) {// 退出群组
        handle_group_change(conn, msg, result);
    }

    void handle_join_group(Connection* conn, const Message& msg, const QueryResult& result) {// 加入群组
        handle_group_change(conn, msg, result);
        if (!result.success || result.group_id == 0 || result.group_members.empty()) return;
        for (const auto& m : result.group_members) {
            if (m.user_id == conn->user_id) continue;
            if (m.role == "owner" || m.role == "admin")
                notify_target(m.user_id, "[系统通知] 用户 " + conn->username + " 加入了群组 " + std::to_string(result.group_id));
        }
    }

    void handle_approve_join_group(Connection* conn, const Message& msg, const QueryResult& result) {// 申请加入群组
        handle_group_change(conn, msg, result);
        if (result.success && msg.target_id != 0)
            notify_target(msg.target_id, "[系统通知] 你已被批准加入群组 " + std::to_string(result.group_id));
    }

    void handle_remove_group_member(Connection* conn, const Message& msg, const QueryResult& result) {// 被移除群组
        handle_group_change(conn, msg, result);
        if (result.success && msg.target_id != 0)
            notify_target(msg.target_id, "[系统通知] 你已被管理员移出群组 " + std::to_string(msg.group_id));
    }

    void handle_reject_join_group(Connection* conn, const Message& msg, const QueryResult& result) {// 被拒绝加入群组
        handle_group_change(conn, msg, result);
        if (result.success && msg.target_id != 0)
            notify_target(msg.target_id, "[系统通知] 你加入群组 " + std::to_string(msg.group_id) + " 的请求已被拒绝");
    }

    void handle_dismiss_group(Connection* conn, const Message& msg, const QueryResult& result) {// 解散群组
        handle_group_change(conn, msg, result);
        if (!result.success || result.group_members.empty()) return;
        for (const auto& m : result.group_members) {
            if (m.user_id == conn->user_id) continue;
            notify_target(m.user_id, "[系统通知] 群组 " + std::to_string(result.group_id) + " 已被群主解散");
        }
    }

    void dispatch_chat(Connection* conn, const Message& msg, const QueryResult& result) {// 聊天模块
        switch (msg.type) {
            case MessageType::PRIVATE_CHAT_REQ:  handle_private_chat(conn, msg, result); break;
            case MessageType::GROUP_CHAT_REQ:    handle_group_chat(conn, msg, result); break;
            case MessageType::GET_HISTORY_REQ:   send_rsp(conn, msg, result); break;
            default: break;
        }
    }

    void handle_private_chat(Connection* conn, const Message& msg, const QueryResult& result) {// 私聊
        send_rsp(conn, msg, result);// 发送给响应者
        if (!result.success) return;

        int target_fd = lookup_(msg.target_id);
        if (target_fd < 0) return;
        ChatMessage all_mag;
        all_mag.set_type(static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP));
        all_mag.set_sender_id(msg.sender_id);
        all_mag.set_target_id(msg.target_id);
        all_mag.set_timestamp(msg.timestamp);
        auto* body = all_mag.mutable_private_chat_rsp();  
        body->set_success(true);
        body->set_content(msg.payload);
        body->set_sender_name(conn->username);
        std::string s;
        all_mag.SerializeToString(&s);
        sender_(target_fd, s);// 发送给接收者
    }

    void handle_group_chat(Connection* conn, const Message& msg, const QueryResult& result) {// 群聊
        send_rsp(conn, msg, result);
        if (!result.success) return;

        for (const auto& m : result.group_members) {
            if (m.user_id == conn->user_id) continue;
            int fd = lookup_(m.user_id);
            if (fd < 0) continue;
            ChatMessage all_mag;
            all_mag.set_type(static_cast<uint32_t>(MessageType::GROUP_CHAT_RSP));
            all_mag.set_sender_id(msg.sender_id);
            all_mag.set_group_id(msg.group_id);
            all_mag.set_timestamp(msg.timestamp);
            auto* body = all_mag.mutable_group_chat_rsp();
            body->set_success(true);
            body->set_content(msg.payload);
            body->set_sender_name(conn->username);
            std::string s;
            all_mag.SerializeToString(&s);
            sender_(fd, s);
        }
    }

    void dispatch_file_send(Connection* conn, const Message& msg, const QueryResult& result) {// 文件模块
        switch (msg.type) {
            case MessageType::FILE_SEND_REQ:            handle_file_send(conn, msg, result); break;
            case MessageType::FILE_SEND_CHUNK_REQ:      send_rsp(conn, msg, result); break;
            case MessageType::FILE_TRANSFER_ACCEPT_REQ: handle_file_transfer_accept(conn, msg, result); break;
            case MessageType::FILE_RECEIVE_CHUNK_REQ:   send_rsp(conn, msg, result); break;
             case MessageType::FILE_TRANSFER_STATUS_REQ: send_rsp(conn, msg, result); break;
             case MessageType::FILE_FINALIZE_REQ:        handle_file_finalize(conn, msg, result); break;
             default: break;
        }
    }

    void handle_file_send(Connection* conn, const Message& msg, const QueryResult& result) {
        send_rsp(conn, msg, result);
        if (!result.success || result.transfer_id == 0) return;

        int target_fd = lookup_(msg.target_id);
        ChatMessage notify;
        notify.set_type(static_cast<uint32_t>(MessageType::FILE_TRANSFER_NOTIFY));
        notify.set_sender_id(msg.sender_id);
        notify.set_target_id(msg.target_id);
        auto* nb = notify.mutable_file_transfer_notify();
        nb->set_transfer_id(result.transfer_id);
        nb->set_sender_id(msg.sender_id);
        nb->set_sender_name(conn->username);
        nb->set_file_name(msg.payload);
        nb->set_file_size(msg.file_size);
        nb->set_total_chunks(msg.total_chunks > 0 ? msg.total_chunks : 1);
        if (!msg.file_hash.empty())
            nb->set_file_hash(msg.file_hash);
        std::string s;
        notify.SerializeToString(&s);
        if (target_fd >= 0) {
            sender_(target_fd, s);
        }
    }

    void handle_file_transfer_accept(Connection* conn, const Message& msg, const QueryResult& result) {// 文件准备发送通知
        send_rsp(conn, msg, result);
        if (!result.success) return;
        notify_target(msg.target_id, "[系统通知] 用户 " + conn->username
                      + " 已接受文件传输 #" + std::to_string(result.transfer_id));
    }

    void handle_file_finalize(Connection* conn, const Message& msg, const QueryResult& result) {// 文件发送完毕通知
        send_rsp(conn, msg, result);
        if (result.success) {
            std::string notify_msg = "[系统通知] 用户 " + conn->username
                                   + " 的文件传输已完成: " + (result.file_name.empty() ? "未知文件" : result.file_name);
            notify_target(result.target_user_id, notify_msg);
        }
    }

    SendFunc sender_;
    LookupFunc lookup_;
};

} // namespace chatroom