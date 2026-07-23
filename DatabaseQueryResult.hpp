#pragma once

#include "MessageType.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace chatroom {

struct QueryResult {
    bool success = false;
    std::string error_message;
    
    // ===== 通用字段 =====
    uint64_t user_id = 0;
    std::string username;
    std::string nickname;
    bool is_online = false;
    
    // ===== 好友相关 =====
    struct FriendInfo {
        uint64_t user_id;
        std::string username;
        std::string nickname;
        bool is_online = false;
        bool is_blocked = false;
        uint64_t add_time = 0;
    };
    std::vector<FriendInfo> friend_list;
    
    // ===== 群组相关 =====
    struct GroupInfo {
        uint64_t group_id;
        std::string group_name;
        std::string description;
        uint64_t owner_id;
        uint64_t member_count;
        bool is_member = false;
    };
    std::vector<GroupInfo> group_list;
    
    struct GroupMember {
        uint64_t user_id;
        std::string username;
        std::string nickname;
        std::string role;  // "owner", "admin", "member"
        uint64_t join_time = 0;
    };
    std::vector<GroupMember> group_members;
    
    // ===== 消息相关 =====
    struct MessageHistory {
        uint64_t message_id;
        uint64_t sender_id;
        std::string sender_name;
        std::string content;
        uint64_t timestamp = 0;
        bool is_read = false;
    };
    std::vector<MessageHistory> history;
    
    // ===== 离线消息 =====
    std::vector<MessageHistory> offline_messages;
    
    // ===== 文件相关 =====
    struct FileInfo {
        uint64_t file_id;
        std::string file_name;
        uint64_t file_size;
        std::string file_path;
        uint64_t sender_id;
        std::string sender_name;
        uint64_t timestamp = 0;
        bool is_downloaded = false;
    };
    std::vector<FileInfo> offline_files;
    
    // ===== 其他 =====
    std::string token;
    std::string verify_code; 
};

} // namespace chatroom