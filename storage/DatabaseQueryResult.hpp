#pragma once

#include "service/MessageType.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace chatroom {

struct QueryResult {
    bool success = false;
    std::string error_message;
    
    
    uint64_t user_id = 0;
    std::string username;
    std::string nickname;
    bool is_online = false;
    int user_status = 1;  

    uint64_t file_id = 0;
    std::string file_name;
    uint64_t file_size = 0;
    std::string file_data;  
    uint32_t chunk_seq = 0;
    uint32_t total_chunks = 0;
    std::vector<uint32_t> received_chunks;  

    uint64_t transfer_id = 0;
    uint64_t target_user_id = 0;  
    std::string transfer_status;  
    std::vector<uint32_t> sender_received_chunks;    
    std::vector<uint32_t> receiver_received_chunks;  
    std::string sender_name;  
    std::string chunk_hash;   
    std::string file_hash;    
    
    uint64_t group_id = 0;
    struct FriendInfo {
        uint64_t user_id;
        std::string username;
        std::string nickname;
        bool is_online = false;
        bool is_blocked = false;
        uint64_t add_time = 0;
        uint64_t streak_days = 0;  // 续火花天数
    };
    std::vector<FriendInfo> friend_list;
    struct GroupInfo {
        uint64_t group_id;
        std::string group_name;
        std::string description;
        uint64_t owner_id;
        uint64_t member_count;
        bool is_member = false;
        bool is_public = true;
    };
    std::vector<GroupInfo> group_list;
    
    struct GroupMember {
        uint64_t user_id;
        std::string username;
        std::string nickname;
        std::string role;
        uint64_t join_time = 0;
    };
    std::vector<GroupMember> group_members;
    struct MessageHistory {
        uint64_t message_id;
        uint64_t sender_id;
        std::string sender_name;
        std::string content;
        uint64_t timestamp = 0;
        bool is_read = false;
    };
    std::vector<MessageHistory> history;

    std::vector<MessageHistory> offline_messages;

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

    std::string token;
    std::string verify_code;
    int expire_seconds = 0;    // 验证码有效期
    int resend_seconds = 0;    // 重发冷却
    std::string debug_code;
    std::string final_path;
};

} // namespace chatroom