#pragma once

#include "MessageType.hpp"
#include "storage/DatabaseQueryResult.hpp"
#include "chatroom.pb.h"
#include <string>
#include <cstring>
#include <iostream>

namespace chatroom {

class MessageParser {
public:
    static bool parse(const char* buffer, int len, Message& msg) {
        if (!buffer || len <= 0) {
            std::cerr << "[MessageParser] Invalid buffer" << std::endl;
            return false;
        }

        ChatMessage proto_msg;
        if (!proto_msg.ParseFromArray(buffer, len)) {
            std::cerr << "[MessageParser] Protobuf parse failed" << std::endl;
            return false;
        }

        // ===== 填充 Message 结构体 =====
        msg.type      = static_cast<MessageType>(proto_msg.type());
        msg.sender_id = proto_msg.sender_id();
        msg.target_id = proto_msg.target_id();
        msg.group_id  = proto_msg.group_id();
        msg.timestamp = proto_msg.timestamp();
        msg.token     = proto_msg.token();

        // ===== 根据 oneof body 类型提取 payload =====
        switch (proto_msg.body_case()) {
            case ChatMessage::kLoginReq:
                msg.payload = proto_msg.login_req().username() + "\n"
                            + proto_msg.login_req().password();
                break;
            case ChatMessage::kRegisterReq:
                msg.payload = proto_msg.register_req().username() + "\n"
                            + proto_msg.register_req().password() + "\n"
                            + proto_msg.register_req().nickname();
                break;
            case ChatMessage::kLogoutReq:
                msg.payload = "";
                break;
            case ChatMessage::kDeleteAccountReq:
                msg.payload = proto_msg.delete_account_req().password();
                break;
            case ChatMessage::kAddFriendReq:
                msg.payload = std::to_string(proto_msg.add_friend_req().target_user_id());
                break;
            case ChatMessage::kDeleteFriendReq:
                msg.payload = std::to_string(proto_msg.delete_friend_req().target_user_id());
                break;
            case ChatMessage::kQueryFriendReq:
                msg.payload = "";
                break;
            case ChatMessage::kBlockFriendReq:
                msg.payload = std::to_string(proto_msg.block_friend_req().target_user_id());
                break;
            case ChatMessage::kUnblockFriendReq:
                msg.payload = std::to_string(proto_msg.unblock_friend_req().target_user_id());
                break;
            case ChatMessage::kCreateGroupReq:
                msg.payload = proto_msg.create_group_req().group_name() + "\n"
                            + proto_msg.create_group_req().description();
                break;
            case ChatMessage::kDismissGroupReq:
                msg.payload = std::to_string(proto_msg.dismiss_group_req().group_id());
                break;
            case ChatMessage::kJoinGroupReq:
                msg.payload = std::to_string(proto_msg.join_group_req().group_id());
                break;
            case ChatMessage::kQuitGroupReq:
                msg.payload = std::to_string(proto_msg.quit_group_req().group_id());
                break;
            case ChatMessage::kQueryGroupListReq:
                msg.payload = "";
                break;
            case ChatMessage::kQueryGroupMembersReq:
                msg.payload = std::to_string(proto_msg.query_group_members_req().group_id());
                break;
            case ChatMessage::kAddGroupAdminReq:
                msg.payload = std::to_string(proto_msg.add_group_admin_req().group_id()) + "\n"
                            + std::to_string(proto_msg.add_group_admin_req().target_user_id());
                msg.group_id = proto_msg.add_group_admin_req().group_id();
                msg.target_id = proto_msg.add_group_admin_req().target_user_id();
                break;
            case ChatMessage::kRemoveGroupAdminReq:
                msg.payload = std::to_string(proto_msg.remove_group_admin_req().group_id()) + "\n"
                            + std::to_string(proto_msg.remove_group_admin_req().target_user_id());
                msg.group_id = proto_msg.remove_group_admin_req().group_id();
                msg.target_id = proto_msg.remove_group_admin_req().target_user_id();
                break;
            case ChatMessage::kApproveJoinGroupReq:
                msg.payload = std::to_string(proto_msg.approve_join_group_req().group_id()) + "\n"
                            + std::to_string(proto_msg.approve_join_group_req().target_user_id());
                msg.group_id = proto_msg.approve_join_group_req().group_id();
                msg.target_id = proto_msg.approve_join_group_req().target_user_id();
                break;
            case ChatMessage::kRemoveGroupMemberReq:
                msg.payload = std::to_string(proto_msg.remove_group_member_req().group_id()) + "\n"
                            + std::to_string(proto_msg.remove_group_member_req().target_user_id());
                msg.group_id = proto_msg.remove_group_member_req().group_id();
                msg.target_id = proto_msg.remove_group_member_req().target_user_id();
                break;
            case ChatMessage::kPrivateChatReq:
                msg.payload = proto_msg.private_chat_req().payload();
                break;
            case ChatMessage::kGroupChatReq:
                msg.payload = proto_msg.group_chat_req().payload();
                break;
            case ChatMessage::kGetHistoryReq:
                msg.payload = std::to_string(proto_msg.get_history_req().target_user_id()) + "\n"
                            + std::to_string(proto_msg.get_history_req().group_id()) + "\n"
                            + std::to_string(proto_msg.get_history_req().limit());
                break;
            case ChatMessage::kFileUploadReq:
                msg.payload = proto_msg.file_upload_req().file_name();
                msg.file_data = proto_msg.file_upload_req().file_data();
                msg.file_size = proto_msg.file_upload_req().file_size();
                msg.chunk_seq = proto_msg.file_upload_req().chunk_seq();
                msg.total_chunks = proto_msg.file_upload_req().total_chunks();
                break;
            case ChatMessage::kFileDownloadReq:
                msg.payload = std::to_string(proto_msg.file_download_req().file_id());
                break;
            case ChatMessage::kFileUploadChunkReq:
                msg.payload = proto_msg.file_upload_chunk_req().file_name();
                msg.file_data = proto_msg.file_upload_chunk_req().file_data();
                msg.file_size = proto_msg.file_upload_chunk_req().file_size();
                msg.chunk_seq = proto_msg.file_upload_chunk_req().chunk_seq();
                msg.total_chunks = proto_msg.file_upload_chunk_req().total_chunks();
                break;
            default:
                // 响应消息或未知消息：payload 保持为空
                break;
        }

        // ===== 自动设置属性标志 =====
        msg.flags = get_flags_by_type(msg.type);

        return true;
    }

    /**
     * @brief 序列化 Message 结构体为 Protobuf 字节流
     */
    static std::string serialize(const Message& msg) {
        ChatMessage proto_msg;

        proto_msg.set_type(static_cast<uint32_t>(msg.type));
        proto_msg.set_sender_id(msg.sender_id);
        proto_msg.set_target_id(msg.target_id);
        proto_msg.set_group_id(msg.group_id);
        proto_msg.set_timestamp(msg.timestamp);
        proto_msg.set_token(msg.token);

        std::string result;
        if (!proto_msg.SerializeToString(&result)) {
            std::cerr << "[MessageParser] Serialize failed" << std::endl;
            return "";
        }
        return result;
    }

    /**
     * @brief 根据 QueryResult 序列化完整的响应消息
     */
    static std::string serialize_response(const Message& request_msg, const QueryResult& result) {
        ChatMessage proto_msg;

        // 响应类型 = 请求类型 + 1
        uint32_t response_type = static_cast<uint32_t>(request_msg.type) + 1;
        proto_msg.set_type(response_type);
        proto_msg.set_sender_id(request_msg.sender_id);
        proto_msg.set_target_id(request_msg.target_id);
        proto_msg.set_group_id(request_msg.group_id);
        
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        proto_msg.set_timestamp(timestamp);

        switch (request_msg.type) {
            case MessageType::LOGIN_REQ: {
                auto* body = proto_msg.mutable_login_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_user_id(result.user_id);
                    body->set_username(result.username);
                    body->set_nickname(result.nickname);
                    body->set_token(result.token);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::REGISTER_REQ: {
                auto* body = proto_msg.mutable_register_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_user_id(result.user_id);
                    body->set_username(result.username);
                    body->set_token(result.token);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::LOGOUT_REQ: {
                auto* body = proto_msg.mutable_logout_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::DELETE_ACCOUNT_REQ: {
                auto* body = proto_msg.mutable_delete_account_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::ADD_FRIEND_REQ: {
                auto* body = proto_msg.mutable_add_friend_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::DELETE_FRIEND_REQ: {
                auto* body = proto_msg.mutable_delete_friend_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::QUERY_FRIEND_REQ: {
                auto* body = proto_msg.mutable_query_friend_rsp();
                body->set_success(result.success);
                if (result.success) {
                    for (const auto& f : result.friend_list) {
                        auto* info = body->add_friends();
                        info->set_user_id(f.user_id);
                        info->set_username(f.username);
                        info->set_nickname(f.nickname);
                        info->set_is_online(f.is_online);
                        info->set_is_blocked(f.is_blocked);
                        info->set_add_time(f.add_time);
                    }
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::BLOCK_FRIEND_REQ: {
                auto* body = proto_msg.mutable_block_friend_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::UNBLOCK_FRIEND_REQ: {
                auto* body = proto_msg.mutable_unblock_friend_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::CREATE_GROUP_REQ: {
                auto* body = proto_msg.mutable_create_group_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_group_id(result.group_id);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::DISMISS_GROUP_REQ: {
                auto* body = proto_msg.mutable_dismiss_group_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::JOIN_GROUP_REQ: {
                auto* body = proto_msg.mutable_join_group_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_group_id(result.group_id);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::QUIT_GROUP_REQ: {
                auto* body = proto_msg.mutable_quit_group_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::QUERY_GROUP_LIST_REQ: {
                auto* body = proto_msg.mutable_query_group_list_rsp();
                body->set_success(result.success);
                if (result.success) {
                    for (const auto& g : result.group_list) {
                        auto* info = body->add_groups();
                        info->set_group_id(g.group_id);
                        info->set_group_name(g.group_name);
                        info->set_description(g.description);
                        info->set_owner_id(g.owner_id);
                        info->set_member_count(g.member_count);
                        info->set_is_member(g.is_member);
                    }
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::QUERY_GROUP_MEMBERS_REQ: {
                auto* body = proto_msg.mutable_query_group_members_rsp();
                body->set_success(result.success);
                if (result.success) {
                    for (const auto& m : result.group_members) {
                        auto* member = body->add_members();
                        member->set_user_id(m.user_id);
                        member->set_username(m.username);
                        member->set_nickname(m.nickname);
                        member->set_role(m.role);
                        member->set_join_time(m.join_time);
                    }
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::ADD_GROUP_ADMIN_REQ: {
                auto* body = proto_msg.mutable_add_group_admin_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::REMOVE_GROUP_ADMIN_REQ: {
                auto* body = proto_msg.mutable_remove_group_admin_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::APPROVE_JOIN_GROUP_REQ: {
                auto* body = proto_msg.mutable_approve_join_group_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_group_id(result.group_id);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::REMOVE_GROUP_MEMBER_REQ: {
                auto* body = proto_msg.mutable_remove_group_member_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::PRIVATE_CHAT_REQ: {
                auto* body = proto_msg.mutable_private_chat_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::GROUP_CHAT_REQ: {
                auto* body = proto_msg.mutable_group_chat_rsp();
                body->set_success(result.success);
                if (!result.success) {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::GET_HISTORY_REQ: {
                auto* body = proto_msg.mutable_get_history_rsp();
                body->set_success(result.success);
                if (result.success) {
                    for (const auto& h : result.history) {
                        auto* msg_item = body->add_messages();
                        msg_item->set_message_id(h.message_id);
                        msg_item->set_sender_id(h.sender_id);
                        msg_item->set_sender_name(h.sender_name);
                        msg_item->set_content(h.content);
                        msg_item->set_timestamp(h.timestamp);
                        msg_item->set_is_read(h.is_read);
                    }
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::FILE_UPLOAD_REQ: {
                auto* body = proto_msg.mutable_file_upload_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_file_id(result.file_id);
                    body->set_file_name(result.file_name);
                    body->set_file_size(result.file_size);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::FILE_DOWNLOAD_REQ: {
                auto* body = proto_msg.mutable_file_download_rsp();
                body->set_success(result.success);
                if (result.success) {
                    if (!result.offline_files.empty()) {
                        const auto& file = result.offline_files[0];
                        body->set_file_id(file.file_id);
                        body->set_file_name(file.file_name);
                        body->set_file_size(file.file_size);
                    }
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            case MessageType::FILE_UPLOAD_CHUNK_REQ: {
                auto* body = proto_msg.mutable_file_upload_chunk_rsp();
                body->set_success(result.success);
                if (result.success) {
                    body->set_file_id(result.file_id);
                    body->set_file_name(result.file_name);
                    body->set_file_size(result.file_size);
                } else {
                    body->set_error_message(result.error_message);
                }
                break;
            }
            default:
                break;
        }

        std::string serialized;
        if (!proto_msg.SerializeToString(&serialized)) {
            std::cerr << "[MessageParser] Serialize failed" << std::endl;
            return "";
        }
        return serialized;
    }

    /**
     * @brief 构造实时通知消息
     */
    static std::string build_notification(uint64_t from_user_id,
                                           const std::string& from_username,
                                           const std::string& notification_type,
                                           const std::string& content) {
        ChatMessage proto_msg;
        proto_msg.set_type(static_cast<uint32_t>(MessageType::OFFLINE_MSG_NOTIFY));
        proto_msg.set_sender_id(from_user_id);
        
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        proto_msg.set_timestamp(timestamp);

        // 使用 private_chat_req 的 payload 来传递通知内容
        auto* chat_content = proto_msg.mutable_private_chat_rsp();
        chat_content->set_success(true);

        std::string serialized;
        if (!proto_msg.SerializeToString(&serialized)) {
            std::cerr << "[MessageParser] Notification serialize failed" << std::endl;
            return "";
        }
        return serialized;
    }

private:
    static uint32_t get_flags_by_type(MessageType type) {
        uint32_t flags = 0;
        int type_val = static_cast<int>(type);

        // 判断是请求还是响应
        if (type_val % 2 == 1) {
            flags |= static_cast<uint32_t>(MessageFlag::IS_REQUEST);
        } else if (type_val % 2 == 0 && type_val != 0) {
            flags |= static_cast<uint32_t>(MessageFlag::IS_RESPONSE);
        }

        if (type != MessageType::LOGIN_REQ &&
            type != MessageType::LOGIN_RSP &&
            type != MessageType::REGISTER_REQ &&
            type != MessageType::REGISTER_RSP &&
            type != MessageType::VERIFY_CODE_REQ &&
            type != MessageType::VERIFY_CODE_RSP &&
            type != MessageType::PASSWORD_RESET_REQ &&
            type != MessageType::PASSWORD_RESET_RSP) {
            flags |= static_cast<uint32_t>(MessageFlag::NEED_LOGIN);
        }

        if (type_val >= 100 && type_val < 200) {
            flags |= static_cast<uint32_t>(MessageFlag::NEED_FRIEND);
            flags |= static_cast<uint32_t>(MessageFlag::NEED_DATABASE);
            flags |= static_cast<uint32_t>(MessageFlag::NEED_PERMISSION);
        } else if (type_val >= 200 && type_val < 300) {
            flags |= static_cast<uint32_t>(MessageFlag::NEED_GROUP);
            flags |= static_cast<uint32_t>(MessageFlag::NEED_DATABASE);
            flags |= static_cast<uint32_t>(MessageFlag::NEED_PERMISSION);
        } else if (type_val >= 300 && type_val < 400) {
            flags |= static_cast<uint32_t>(MessageFlag::NEED_DATABASE);
        } else if (type_val >= 400 && type_val < 500) {
            flags |= static_cast<uint32_t>(MessageFlag::NEED_DATABASE);
        }

        return flags;
    }
};

} // namespace chatroom