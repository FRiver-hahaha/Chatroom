#pragma once

#include <cstdint>
#include <string>

namespace chatroom {

// 消息编码
enum class MessageType : uint32_t {
    UNKNOWN = 0,
    
    // ===== 账号模块 1-99 =====
    LOGIN_REQ = 1,
    LOGIN_RSP = 2,
    REGISTER_REQ = 3,
    REGISTER_RSP = 4,
    LOGOUT_REQ = 5,
    LOGOUT_RSP = 6,
    VERIFY_CODE_REQ = 7,
    VERIFY_CODE_RSP = 8,
    PASSWORD_RESET_REQ = 9,
    PASSWORD_RESET_RSP = 10,
    DELETE_ACCOUNT_REQ = 11,
    DELETE_ACCOUNT_RSP = 12,
    
    // ===== 好友模块 100-199 =====
    ADD_FRIEND_REQ = 100,
    ADD_FRIEND_RSP = 101,
    DELETE_FRIEND_REQ = 102,
    DELETE_FRIEND_RSP = 103,
    QUERY_FRIEND_REQ = 104,
    QUERY_FRIEND_RSP = 105,
    BLOCK_FRIEND_REQ = 106,
    BLOCK_FRIEND_RSP = 107,
    UNBLOCK_FRIEND_REQ = 108,
    UNBLOCK_FRIEND_RSP = 109,
    QUERY_BLOCKED_REQ = 110,
    QUERY_BLOCKED_RSP = 111,
    
    // ===== 群组模块 200-299 =====
    CREATE_GROUP_REQ = 200,
    CREATE_GROUP_RSP = 201,
    DISMISS_GROUP_REQ = 202,
    DISMISS_GROUP_RSP = 203,
    JOIN_GROUP_REQ = 204,
    JOIN_GROUP_RSP = 205,
    QUIT_GROUP_REQ = 206,
    QUIT_GROUP_RSP = 207,
    QUERY_GROUP_LIST_REQ = 208,
    QUERY_GROUP_LIST_RSP = 209,
    QUERY_GROUP_MEMBERS_REQ = 210,
    QUERY_GROUP_MEMBERS_RSP = 211,
    ADD_GROUP_ADMIN_REQ = 212,
    ADD_GROUP_ADMIN_RSP = 213,
    REMOVE_GROUP_ADMIN_REQ = 214,
    REMOVE_GROUP_ADMIN_RSP = 215,
    APPROVE_JOIN_GROUP_REQ = 216,
    APPROVE_JOIN_GROUP_RSP = 217,
    REMOVE_GROUP_MEMBER_REQ = 218,
    REMOVE_GROUP_MEMBER_RSP = 219,
    REJECT_JOIN_GROUP_REQ = 220,
    REJECT_JOIN_GROUP_RSP = 221,
    
    // ===== 聊天模块 300-399 =====
    PRIVATE_CHAT_REQ = 300,
    PRIVATE_CHAT_RSP = 301,
    GROUP_CHAT_REQ = 302,
    GROUP_CHAT_RSP = 303,
    GET_HISTORY_REQ = 304,
    GET_HISTORY_RSP = 305,
    GET_GROUP_HISTORY_REQ = 306,
    GET_GROUP_HISTORY_RSP = 307,
    OFFLINE_MSG_NOTIFY = 308,

    // ===== 文件发送模块 420-439 =====
    FILE_SEND_REQ = 420,
    FILE_SEND_RSP = 421,
    FILE_SEND_CHUNK_REQ = 422,
    FILE_SEND_CHUNK_RSP = 423,
    FILE_TRANSFER_NOTIFY = 424,
    FILE_TRANSFER_ACCEPT_REQ = 425,
    FILE_TRANSFER_ACCEPT_RSP = 426,
    FILE_RECEIVE_CHUNK_REQ = 427,
    FILE_RECEIVE_CHUNK_RSP = 428,
    FILE_TRANSFER_STATUS_REQ = 429,
    FILE_TRANSFER_STATUS_RSP = 430,
    FILE_FINALIZE_REQ = 431,
    FILE_FINALIZE_RSP = 432,
};

enum class MessageFlag : uint32_t {
    NONE            = 0,
    NEED_LOGIN      = 1 << 0,
    NEED_PERMISSION = 1 << 1,
    NEED_DATABASE   = 1 << 2,
    IS_REQUEST      = 1 << 3,
    IS_RESPONSE     = 1 << 4,
    NEED_FRIEND     = 1 << 5,
    NEED_GROUP      = 1 << 6,
};

struct Message {
    MessageType type = MessageType::UNKNOWN;
    uint32_t flags = 0;
    uint64_t sender_id = 0;
    uint64_t target_id = 0;
    uint64_t group_id = 0;
    std::string payload;
    uint64_t timestamp = 0;
    std::string token;
    
    // 文件传输相关
    std::string file_data;
    uint64_t file_size = 0;
    uint32_t chunk_seq = 0;
    uint32_t total_chunks = 0;
    std::string chunk_hash;  // SHA-256 of file_data (32 bytes raw)
    std::string file_hash;   // SHA-256 of complete file (32 bytes raw)
    
    bool is_request() const {
        return flags & static_cast<uint32_t>(MessageFlag::IS_REQUEST);
    }
    
    bool is_response() const {
        return flags & static_cast<uint32_t>(MessageFlag::IS_RESPONSE);
    }
    
    bool need_login() const {
        return flags & static_cast<uint32_t>(MessageFlag::NEED_LOGIN);
    }
    
    bool need_friend() const {
        return flags & static_cast<uint32_t>(MessageFlag::NEED_FRIEND);
    }
    
    bool need_group() const {
        return flags & static_cast<uint32_t>(MessageFlag::NEED_GROUP);
    }
};

inline std::string message_type_name(MessageType type) {
    switch (type) {
        case MessageType::LOGIN_REQ: return "LOGIN_REQ";
        case MessageType::LOGIN_RSP: return "LOGIN_RSP";
        case MessageType::REGISTER_REQ: return "REGISTER_REQ";
        case MessageType::REGISTER_RSP: return "REGISTER_RSP";
        case MessageType::LOGOUT_REQ: return "LOGOUT_REQ";
        case MessageType::LOGOUT_RSP: return "LOGOUT_RSP";
        case MessageType::DELETE_ACCOUNT_REQ: return "DELETE_ACCOUNT_REQ";
        case MessageType::DELETE_ACCOUNT_RSP: return "DELETE_ACCOUNT_RSP";
        case MessageType::ADD_FRIEND_REQ: return "ADD_FRIEND_REQ";
        case MessageType::ADD_FRIEND_RSP: return "ADD_FRIEND_RSP";
        case MessageType::DELETE_FRIEND_REQ: return "DELETE_FRIEND_REQ";
        case MessageType::DELETE_FRIEND_RSP: return "DELETE_FRIEND_RSP";
        case MessageType::QUERY_FRIEND_REQ: return "QUERY_FRIEND_REQ";
        case MessageType::QUERY_FRIEND_RSP: return "QUERY_FRIEND_RSP";
        case MessageType::BLOCK_FRIEND_REQ: return "BLOCK_FRIEND_REQ";
        case MessageType::BLOCK_FRIEND_RSP: return "BLOCK_FRIEND_RSP";
        case MessageType::UNBLOCK_FRIEND_REQ: return "UNBLOCK_FRIEND_REQ";
        case MessageType::UNBLOCK_FRIEND_RSP: return "UNBLOCK_FRIEND_RSP";
        case MessageType::QUERY_BLOCKED_REQ: return "QUERY_BLOCKED_REQ";
        case MessageType::QUERY_BLOCKED_RSP: return "QUERY_BLOCKED_RSP";
        case MessageType::CREATE_GROUP_REQ: return "CREATE_GROUP_REQ";
        case MessageType::CREATE_GROUP_RSP: return "CREATE_GROUP_RSP";
        case MessageType::DISMISS_GROUP_REQ: return "DISMISS_GROUP_REQ";
        case MessageType::DISMISS_GROUP_RSP: return "DISMISS_GROUP_RSP";
        case MessageType::JOIN_GROUP_REQ: return "JOIN_GROUP_REQ";
        case MessageType::JOIN_GROUP_RSP: return "JOIN_GROUP_RSP";
        case MessageType::QUIT_GROUP_REQ: return "QUIT_GROUP_REQ";
        case MessageType::QUIT_GROUP_RSP: return "QUIT_GROUP_RSP";
        case MessageType::QUERY_GROUP_LIST_REQ: return "QUERY_GROUP_LIST_REQ";
        case MessageType::QUERY_GROUP_LIST_RSP: return "QUERY_GROUP_LIST_RSP";
        case MessageType::QUERY_GROUP_MEMBERS_REQ: return "QUERY_GROUP_MEMBERS_REQ";
        case MessageType::QUERY_GROUP_MEMBERS_RSP: return "QUERY_GROUP_MEMBERS_RSP";
        case MessageType::ADD_GROUP_ADMIN_REQ: return "ADD_GROUP_ADMIN_REQ";
        case MessageType::ADD_GROUP_ADMIN_RSP: return "ADD_GROUP_ADMIN_RSP";
        case MessageType::REMOVE_GROUP_ADMIN_REQ: return "REMOVE_GROUP_ADMIN_REQ";
        case MessageType::REMOVE_GROUP_ADMIN_RSP: return "REMOVE_GROUP_ADMIN_RSP";
        case MessageType::APPROVE_JOIN_GROUP_REQ: return "APPROVE_JOIN_GROUP_REQ";
        case MessageType::APPROVE_JOIN_GROUP_RSP: return "APPROVE_JOIN_GROUP_RSP";
        case MessageType::REMOVE_GROUP_MEMBER_REQ: return "REMOVE_GROUP_MEMBER_REQ";
        case MessageType::REMOVE_GROUP_MEMBER_RSP: return "REMOVE_GROUP_MEMBER_RSP";
        case MessageType::REJECT_JOIN_GROUP_REQ: return "REJECT_JOIN_GROUP_REQ";
        case MessageType::REJECT_JOIN_GROUP_RSP: return "REJECT_JOIN_GROUP_RSP";
        case MessageType::PRIVATE_CHAT_REQ: return "PRIVATE_CHAT_REQ";
        case MessageType::PRIVATE_CHAT_RSP: return "PRIVATE_CHAT_RSP";
        case MessageType::GROUP_CHAT_REQ: return "GROUP_CHAT_REQ";
        case MessageType::GROUP_CHAT_RSP: return "GROUP_CHAT_RSP";
        case MessageType::GET_HISTORY_REQ: return "GET_HISTORY_REQ";
        case MessageType::GET_HISTORY_RSP: return "GET_HISTORY_RSP";
        case MessageType::FILE_SEND_REQ: return "FILE_SEND_REQ";
        case MessageType::FILE_SEND_RSP: return "FILE_SEND_RSP";
        case MessageType::FILE_SEND_CHUNK_REQ: return "FILE_SEND_CHUNK_REQ";
        case MessageType::FILE_SEND_CHUNK_RSP: return "FILE_SEND_CHUNK_RSP";
        case MessageType::FILE_TRANSFER_NOTIFY: return "FILE_TRANSFER_NOTIFY";
        case MessageType::FILE_TRANSFER_ACCEPT_REQ: return "FILE_TRANSFER_ACCEPT_REQ";
        case MessageType::FILE_TRANSFER_ACCEPT_RSP: return "FILE_TRANSFER_ACCEPT_RSP";
        case MessageType::FILE_RECEIVE_CHUNK_REQ: return "FILE_RECEIVE_CHUNK_REQ";
        case MessageType::FILE_RECEIVE_CHUNK_RSP: return "FILE_RECEIVE_CHUNK_RSP";
        case MessageType::FILE_TRANSFER_STATUS_REQ: return "FILE_TRANSFER_STATUS_REQ";
        case MessageType::FILE_TRANSFER_STATUS_RSP: return "FILE_TRANSFER_STATUS_RSP";
        case MessageType::FILE_FINALIZE_REQ: return "FILE_FINALIZE_REQ";
        case MessageType::FILE_FINALIZE_RSP: return "FILE_FINALIZE_RSP";
        default: return "UNKNOWN";
    }
}

} // namespace chatroom