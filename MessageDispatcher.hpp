#pragma once

#include "MessageType.hpp"
#include "DatabaseQueryResult.hpp"
#include <memory>
#include <iostream>
#include <functional>
#include <string>

namespace chatroom {

struct Connection;

class MessageDispatcher {
public:
    using SendFunc = std::function<void(int, const std::string&)>;

    explicit MessageDispatcher(SendFunc sender) : sender_(std::move(sender)) {}
    
    void dispatch(Connection* conn,
                  const Message& msg,
                  const QueryResult& query_result) {
        if (!conn) {
            std::cerr << "[MessageDispatcher] Connection is null" << std::endl;
            return;
        }
        
        std::cout << "[MessageDispatcher] Dispatching: " 
                  << message_type_name(msg.type) << std::endl;
        
        int type_val = static_cast<int>(msg.type);
        
        // 根据消息类型路由
        if (type_val >= 1 && type_val < 100) {
            // 账号模块
            dispatch_account(conn, msg, query_result);
        } else if (type_val >= 100 && type_val < 200) {
            // 好友模块
            dispatch_friend(conn, msg, query_result);
        } else if (type_val >= 200 && type_val < 300) {
            // 群组模块
            dispatch_group(conn, msg, query_result);
        } else if (type_val >= 300 && type_val < 400) {
            // 聊天模块
            dispatch_chat(conn, msg, query_result);
        } else if (type_val >= 400 && type_val < 500) {
            // 文件模块
            dispatch_file(conn, msg, query_result);
        } else {
            std::cerr << "[MessageDispatcher] Unknown message type: " 
                      << type_val << std::endl;
        }
    }

private:
    void dispatch_account(Connection* conn, 
                          const Message& msg, 
                          const QueryResult& result) {
        switch (msg.type) {
            case MessageType::LOGIN_REQ:
                handle_login(conn, msg, result);
                break;
            case MessageType::LOGIN_RSP:
                break;
            case MessageType::REGISTER_REQ:
                handle_register(conn, msg, result);
                break;
            case MessageType::LOGOUT_REQ:
                handle_logout(conn, msg, result);
                break;
            default:
                break;
        }
    }
    
    void handle_login(Connection* conn, 
                      const Message& msg, 
                      const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "登录失败: " + result.error_message);
            return;
        }
        
        conn->user_id = result.user_id;
        conn->username = result.username;
        conn->state = SessionState::LOGGED_IN;
        conn->session_token = result.token;
        
        sender_(conn->fd, "登录成功！欢迎 " + result.nickname);
        
        if (!result.offline_messages.empty()) {
            sender_(conn->fd, "你有 " + 
                std::to_string(result.offline_messages.size()) + " 条离线消息");
        }
        
        for (const auto& friend_info : result.friend_list) {
            if (friend_info.is_online) {
                sender_(friend_info.user_id, "好友上线通知");
            }
        }
    }
    
    void handle_register(Connection* conn, 
                         const Message& msg, 
                         const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "注册失败: " + result.error_message);
            return;
        }
        
        sender_(conn->fd, "注册成功！用户ID: " + 
            std::to_string(result.user_id));
    }
    
    void handle_logout(Connection* conn, 
                       const Message& msg, 
                       const QueryResult& result) {
        sender_(conn->fd, "已注销");
        conn->logout();
    }
    
    // ===== 好友模块分发 =====
    void dispatch_friend(Connection* conn, 
                         const Message& msg, 
                         const QueryResult& result) {
        switch (msg.type) {
            case MessageType::ADD_FRIEND_REQ:
                handle_add_friend(conn, msg, result);
                break;
            case MessageType::DELETE_FRIEND_REQ:
                handle_delete_friend(conn, msg, result);
                break;
            case MessageType::QUERY_FRIEND_REQ:
                handle_query_friend(conn, msg, result);
                break;
            case MessageType::BLOCK_FRIEND_REQ:
                handle_block_friend(conn, msg, result);
                break;
            default:
                break;
        }
    }
    
    void handle_add_friend(Connection* conn, 
                           const Message& msg, 
                           const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "添加好友失败: " + result.error_message);
            return;
        }
        
        sender_(conn->fd, "添加好友成功");
    }
    
    void handle_delete_friend(Connection* conn, 
                              const Message& msg, 
                              const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "删除好友失败");
            return;
        }
        
        sender_(conn->fd, "删除好友成功");
    }
    
    void handle_query_friend(Connection* conn, 
                             const Message& msg, 
                             const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "查询好友失败");
            return;
        }
        
        std::string response = "好友列表:\n";
        for (const auto& friend_info : result.friend_list) {
            response += "- " + friend_info.nickname + 
                       " (" + (friend_info.is_online ? "在线" : "离线") + ")\n";
        }
        sender_(conn->fd, response);
    }
    
    void handle_block_friend(Connection* conn, 
                             const Message& msg, 
                             const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "屏蔽好友失败");
            return;
        }
        
        sender_(conn->fd, "屏蔽好友成功");
    }
    
    void dispatch_group(Connection* conn, 
                        const Message& msg, 
                        const QueryResult& result) {
        switch (msg.type) {
            case MessageType::CREATE_GROUP_REQ:
                handle_create_group(conn, msg, result);
                break;
            case MessageType::JOIN_GROUP_REQ:
                handle_join_group(conn, msg, result);
                break;
            case MessageType::QUIT_GROUP_REQ:
                handle_quit_group(conn, msg, result);
                break;
            case MessageType::QUERY_GROUP_LIST_REQ:
                handle_query_group_list(conn, msg, result);
                break;
            case MessageType::QUERY_GROUP_MEMBERS_REQ:
                handle_query_group_members(conn, msg, result);
                break;
            default:
                break;
        }
    }
    
    void handle_create_group(Connection* conn, 
                             const Message& msg, 
                             const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "创建群组失败");
            return;
        }
        
        sender_(conn->fd, "群组创建成功");
    }
    
    void handle_join_group(Connection* conn, 
                           const Message& msg, 
                           const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "申请加入群组失败");
            return;
        }
        
        sender_(conn->fd, "已申请加入群组，等待管理员审批");
    }
    
    void handle_quit_group(Connection* conn, 
                           const Message& msg, 
                           const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "退出群组失败");
            return;
        }
        
        sender_(conn->fd, "已退出群组");
    }
    
    void handle_query_group_list(Connection* conn, 
                                 const Message& msg, 
                                 const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "查询群组列表失败");
            return;
        }
        
        std::string response = "我的群组:\n";
        for (const auto& group : result.group_list) {
            response += "- " + group.group_name + 
                       " (" + std::to_string(group.member_count) + "人)\n";
        }
        sender_(conn->fd, response);
    }
    
    void handle_query_group_members(Connection* conn, 
                                    const Message& msg, 
                                    const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "查询群组成员失败");
            return;
        }
        
        std::string response = "群组成员:\n";
        for (const auto& member : result.group_members) {
            response += "- " + member.nickname + 
                       " (" + member.role + ")\n";
        }
        sender_(conn->fd, response);
    }
    
    // ===== 聊天模块分发 =====
    void dispatch_chat(Connection* conn, 
                       const Message& msg, 
                       const QueryResult& result) {
        switch (msg.type) {
            case MessageType::PRIVATE_CHAT_REQ:
                handle_private_chat(conn, msg, result);
                break;
            case MessageType::GROUP_CHAT_REQ:
                handle_group_chat(conn, msg, result);
                break;
            case MessageType::GET_HISTORY_REQ:
                handle_get_history(conn, msg, result);
                break;
            default:
                break;
        }
    }
    
    void handle_private_chat(Connection* conn, 
                             const Message& msg, 
                             const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "发送失败: " + result.error_message);
            return;
        }
        
        // ===== 转发消息给目标用户 =====
        // 实际项目中需要找到目标用户的连接
        // auto target_conn = server_.get_connection_by_user_id(msg.target_id);
        // if (target_conn) {
        //     sender_(target_conn->fd, "来自 " + conn->username + ": " + msg.payload);
        // }
        
        sender_(conn->fd, "消息已发送");
    }
    
    void handle_group_chat(Connection* conn, 
                           const Message& msg, 
                           const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "发送失败: " + result.error_message);
            return;
        }
        
        // ===== 广播给群组所有成员 =====
        // 实际项目中需要遍历群组成员并发送
        
        sender_(conn->fd, "群消息已发送");
    }
    
    void handle_get_history(Connection* conn, 
                            const Message& msg, 
                            const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "查询历史消息失败");
            return;
        }
        
        std::string response = "历史消息:\n";
        for (const auto& history : result.history) {
            response += "[" + std::to_string(history.timestamp) + "] " +
                       history.sender_name + ": " + history.content + "\n";
        }
        sender_(conn->fd, response);
    }
    
    // ===== 文件模块分发 =====
    void dispatch_file(Connection* conn, 
                       const Message& msg, 
                       const QueryResult& result) {
        switch (msg.type) {
            case MessageType::FILE_UPLOAD_REQ:
                handle_file_upload(conn, msg, result);
                break;
            case MessageType::FILE_DOWNLOAD_REQ:
                handle_file_download(conn, msg, result);
                break;
            default:
                break;
        }
    }
    
    void handle_file_upload(Connection* conn, 
                            const Message& msg, 
                            const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "文件上传失败");
            return;
        }
        
        sender_(conn->fd, "文件上传成功");
    }
    
    void handle_file_download(Connection* conn, 
                              const Message& msg, 
                              const QueryResult& result) {
        if (!result.success) {
            sender_(conn->fd, "文件下载失败");
            return;
        }
        
        sender_(conn->fd, "文件下载成功");
    }

private:
    SendFunc sender_;
};

} // namespace chatroom