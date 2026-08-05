#pragma once

#include "service/MessageType.hpp"
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
    int user_status = 1;  // 0=inactive, 1=active, 2=banned
    
    // ===== 文件响应相关 =====
    uint64_t file_id = 0;
    std::string file_name;
    uint64_t file_size = 0;
    std::string file_data;  // 文件下载时的实际内容
    uint32_t chunk_seq = 0;
    uint32_t total_chunks = 0;
    std::vector<uint32_t> received_chunks;  // 断点续传已收分片

    // ===== 文件传输 (420-439) 相关 =====
    uint64_t transfer_id = 0;
    uint64_t target_user_id = 0;  // 接收方用户ID（用于转发）
    std::string transfer_status;  // "sending" | "completed" | "rejected"
    std::vector<uint32_t> sender_received_chunks;    // A 已上传的分片
    std::vector<uint32_t> receiver_received_chunks;  // B 已接收的分片
    std::string sender_name;  // 发送者用户名
    std::string chunk_hash;   // SHA-256 of current chunk
    std::string file_hash;    // SHA-256 of complete file
    
    // ===== 群组相关 =====
    uint64_t group_id = 0;
    
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