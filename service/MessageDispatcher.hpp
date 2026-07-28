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
    using SendFunc = std::function<void(int, const std::string&)>;
    using LookupFunc = std::function<int(uint64_t user_id)>;

    explicit MessageDispatcher(SendFunc sender, LookupFunc lookup)
        : sender_(std::move(sender)), lookup_(std::move(lookup)) {}
    
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
        
        if (type_val >= 1 && type_val < 100) {
            dispatch_account(conn, msg, query_result);
        } else if (type_val >= 100 && type_val < 200) {
            dispatch_friend(conn, msg, query_result);
        } else if (type_val >= 200 && type_val < 300) {
            dispatch_group(conn, msg, query_result);
        } else if (type_val >= 300 && type_val < 400) {
            dispatch_chat(conn, msg, query_result);
        } else if (type_val >= 400 && type_val < 500) {
            dispatch_file(conn, msg, query_result);
        } else {
            std::cerr << "[MessageDispatcher] Unknown message type: " 
                      << type_val << std::endl;
        }
    }

    // ===== 通知方法 =====
    void notify_user(uint64_t user_id, const std::string& message) {
        int fd = lookup_(user_id);
        if (fd >= 0) {
            sender_(fd, message);
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
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
        
        if (!result.success) return;
        
        // 更新连接状态
        conn->user_id = result.user_id;
        conn->username = result.username;
        conn->state = SessionState::LOGGED_IN;
        conn->session_token = result.token;
        
        // 发送离线消息
        if (!result.offline_messages.empty()) {
            Message offline_notify;
            offline_notify.type = MessageType::OFFLINE_MSG_NOTIFY;
            offline_notify.sender_id = 0;
            std::string notify_str = "你有 " + std::to_string(result.offline_messages.size()) + " 条离线消息";
            sender_(conn->fd, notify_str);
            
            for (const auto& offline_msg : result.offline_messages) {
                std::string msg_text = "[离线消息][" + offline_msg.sender_name + "]: " + offline_msg.content;
                sender_(conn->fd, msg_text);
            }
        }
        
        // 通知好友上线
        for (const auto& friend_info : result.friend_list) {
            if (friend_info.is_online) {
                int friend_fd = lookup_(friend_info.user_id);
                if (friend_fd >= 0) {
                    std::string notify = "[系统通知] 好友 " + conn->username + " 上线了";
                    sender_(friend_fd, notify);
                }
            }
        }
    }
    
    void handle_register(Connection* conn, 
                         const Message& msg, 
                         const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_logout(Connection* conn, 
                       const Message& msg, 
                       const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
        
        if (result.success) {
            conn->logout();
        }
    }
    
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
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
        
        // 通知目标用户
        if (result.success && msg.target_id != 0) {
            int target_fd = lookup_(msg.target_id);
            if (target_fd >= 0) {
                std::string notify = "[系统通知] 用户 " + conn->username + " 请求添加你为好友";
                sender_(target_fd, notify);
            }
        }
    }
    
    void handle_delete_friend(Connection* conn, 
                              const Message& msg, 
                              const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_query_friend(Connection* conn, 
                             const Message& msg, 
                             const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_block_friend(Connection* conn, 
                             const Message& msg, 
                             const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void dispatch_group(Connection* conn, 
                        const Message& msg, 
                        const QueryResult& result) {
        switch (msg.type) {
            case MessageType::CREATE_GROUP_REQ:
                handle_create_group(conn, msg, result);
                break;
            case MessageType::DISMISS_GROUP_REQ:
                handle_dismiss_group(conn, msg, result);
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
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_dismiss_group(Connection* conn, 
                              const Message& msg, 
                              const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_join_group(Connection* conn, 
                           const Message& msg, 
                           const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
        
        // 通知群主和管理员
        if (result.success && result.group_id != 0) {
            // TODO: 通知群管理员
        }
    }
    
    void handle_quit_group(Connection* conn, 
                           const Message& msg, 
                           const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_query_group_list(Connection* conn, 
                                 const Message& msg, 
                                 const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_query_group_members(Connection* conn, 
                                    const Message& msg, 
                                    const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
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
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
        
        if (!result.success) return;
        
        // 转发消息给目标用户
        int target_fd = lookup_(msg.target_id);
        if (target_fd >= 0) {
            // 构造转发消息
            Message forward_msg;
            forward_msg.type = MessageType::PRIVATE_CHAT_RSP;
            forward_msg.sender_id = msg.sender_id;
            forward_msg.target_id = msg.target_id;
            
            ChatMessage proto_forward;
            proto_forward.set_type(static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP));
            proto_forward.set_sender_id(msg.sender_id);
            proto_forward.set_target_id(msg.target_id);
            
            auto* chat_body = proto_forward.mutable_private_chat_rsp();
            chat_body->set_success(true);
            
            std::string serialized;
            proto_forward.SerializeToString(&serialized);
            sender_(target_fd, serialized);
        }
    }
    
    void handle_group_chat(Connection* conn,
                           const Message& msg,
                           const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
        
        if (!result.success) return;
        
        // 广播给群组所有在线成员
        for (const auto& member : result.group_members) {
            if (member.user_id == conn->user_id) continue;
            int member_fd = lookup_(member.user_id);
            if (member_fd >= 0) {
                ChatMessage proto_forward;
                proto_forward.set_type(static_cast<uint32_t>(MessageType::GROUP_CHAT_RSP));
                proto_forward.set_sender_id(msg.sender_id);
                proto_forward.set_group_id(msg.group_id);
                
                auto* chat_body = proto_forward.mutable_group_chat_rsp();
                chat_body->set_success(true);
                
                std::string serialized;
                proto_forward.SerializeToString(&serialized);
                sender_(member_fd, serialized);
            }
        }
    }
    
    void handle_get_history(Connection* conn, 
                            const Message& msg, 
                            const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
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
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }
    
    void handle_file_download(Connection* conn, 
                              const Message& msg, 
                              const QueryResult& result) {
        std::string response = MessageParser::serialize_response(msg, result);
        sender_(conn->fd, response);
    }

private:
    SendFunc sender_;
    LookupFunc lookup_;
};

} // namespace chatroom