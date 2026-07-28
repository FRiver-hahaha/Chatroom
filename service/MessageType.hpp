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
    
    // ===== 文件模块 400-499 =====
    FILE_UPLOAD_REQ = 400,
    FILE_UPLOAD_RSP = 401,
    FILE_DOWNLOAD_REQ = 402,
    FILE_DOWNLOAD_RSP = 403,
    FILE_UPLOAD_CHUNK_REQ = 404,
    FILE_UPLOAD_CHUNK_RSP = 405,
    OFFLINE_FILE_NOTIFY = 406,
    
    // ===== 动态模块 500-599 =====
    MOMENT_PUBLISH_REQ = 500,
    MOMENT_PUBLISH_RSP = 501,
    MOMENT_QUERY_REQ = 502,
    MOMENT_QUERY_RSP = 503,
    GAME_START_REQ = 504,
    GAME_START_RSP = 505,
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
        case MessageType::ADD_FRIEND_REQ: return "ADD_FRIEND_REQ";
        case MessageType::ADD_FRIEND_RSP: return "ADD_FRIEND_RSP";
        case MessageType::DELETE_FRIEND_REQ: return "DELETE_FRIEND_REQ";
        case MessageType::DELETE_FRIEND_RSP: return "DELETE_FRIEND_RSP";
        case MessageType::QUERY_FRIEND_REQ: return "QUERY_FRIEND_REQ";
        case MessageType::QUERY_FRIEND_RSP: return "QUERY_FRIEND_RSP";
        case MessageType::BLOCK_FRIEND_REQ: return "BLOCK_FRIEND_REQ";
        case MessageType::BLOCK_FRIEND_RSP: return "BLOCK_FRIEND_RSP";
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
        case MessageType::PRIVATE_CHAT_REQ: return "PRIVATE_CHAT_REQ";
        case MessageType::PRIVATE_CHAT_RSP: return "PRIVATE_CHAT_RSP";
        case MessageType::GROUP_CHAT_REQ: return "GROUP_CHAT_REQ";
        case MessageType::GROUP_CHAT_RSP: return "GROUP_CHAT_RSP";
        case MessageType::GET_HISTORY_REQ: return "GET_HISTORY_REQ";
        case MessageType::GET_HISTORY_RSP: return "GET_HISTORY_RSP";
        case MessageType::FILE_UPLOAD_REQ: return "FILE_UPLOAD_REQ";
        case MessageType::FILE_UPLOAD_RSP: return "FILE_UPLOAD_RSP";
        case MessageType::FILE_DOWNLOAD_REQ: return "FILE_DOWNLOAD_REQ";
        case MessageType::FILE_DOWNLOAD_RSP: return "FILE_DOWNLOAD_RSP";
        default: return "UNKNOWN";
    }
}

} // namespace chatroom