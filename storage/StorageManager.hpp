#pragma once

#include "DatabaseQueryResult.hpp"
#include <mysql/mysql.h>
#include <hiredis/hiredis.h>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace chatroom {

class MySQLConnectionPool {
public:
    MySQLConnectionPool(size_t pool_size = 8);
    ~MySQLConnectionPool();

    bool init(const std::string& host,
              const std::string& user,
              const std::string& password,
              const std::string& database,
              unsigned int port = 3306);

    MYSQL* acquire();
    void release(MYSQL* conn);
    void close_all();

private:
    struct ConnInfo {
        std::string host;
        std::string user;
        std::string password;
        std::string database;
        unsigned int port = 3306;
    };

    MYSQL* create_connection();
    void close_connection(MYSQL* conn);

    ConnInfo conn_info_;
    size_t pool_size_;
    std::queue<MYSQL*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool initialized_ = false;
};

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    bool connect(const std::string& mysql_host,
                 const std::string& mysql_user,
                 const std::string& mysql_password,
                 const std::string& mysql_database,
                 int redis_port = 6379,
                 const std::string& redis_host = "127.0.0.1");
    void disconnect();

    // 账号模块
    bool user_exists(const std::string& username);
    QueryResult create_user(const std::string& username,
                            const std::string& password,
                            const std::string& nickname);
    QueryResult get_user_by_username(const std::string& username);
    QueryResult get_user_by_id(uint64_t user_id);
    bool verify_password(const std::string& username, const std::string& password);
    bool update_password(uint64_t user_id, const std::string& new_password);

    // 状态模块
    std::string create_session(uint64_t user_id, const std::string& username);
    bool verify_token(const std::string& token);
    bool clear_session(uint64_t user_id);
    bool delete_user(uint64_t user_id);

    // 好友模块
    bool add_friend(uint64_t user_id, uint64_t friend_id);
    bool remove_friend(uint64_t user_id, uint64_t friend_id);
    bool is_friend(uint64_t user_id, uint64_t friend_id);
    bool block_friend(uint64_t user_id, uint64_t friend_id);
    bool unblock_friend(uint64_t user_id, uint64_t friend_id);
    bool is_blocked_by(uint64_t user_id, uint64_t friend_id);
    std::vector<QueryResult::FriendInfo> get_friends(uint64_t user_id);
    std::vector<QueryResult::FriendInfo> get_blocked_users(uint64_t user_id);

    // 群组模块
    uint64_t create_group(const std::string& group_name,
                          const std::string& description,
                          uint64_t owner_id,
                          bool is_public = true);
    bool dismiss_group(uint64_t group_id, uint64_t requester_id);
    bool join_group(uint64_t group_id, uint64_t user_id);
    bool quit_group(uint64_t group_id, uint64_t user_id);
    bool add_admin(uint64_t group_id, uint64_t owner_id, uint64_t user_id);
    bool remove_admin(uint64_t group_id, uint64_t owner_id, uint64_t user_id);
    bool approve_join(uint64_t group_id, uint64_t admin_id, uint64_t user_id);
    bool reject_join(uint64_t group_id, uint64_t admin_id, uint64_t user_id);
    bool remove_member(uint64_t group_id, uint64_t admin_id, uint64_t user_id);
    bool is_group_member(uint64_t group_id, uint64_t user_id);
    bool is_group_public(uint64_t group_id);
    bool request_join_group(uint64_t group_id, uint64_t user_id);
    bool transfer_group_ownership(uint64_t group_id, uint64_t new_owner_id);
    bool is_join_pending(uint64_t group_id, uint64_t user_id);
    std::vector<uint64_t> get_pending_join_requests(uint64_t group_id);
    QueryResult::GroupInfo get_group_info(uint64_t group_id);
    std::vector<QueryResult::GroupInfo> get_user_groups(uint64_t user_id);
    std::vector<QueryResult::GroupMember> get_group_members(uint64_t group_id);

    // 消息模块
    bool save_message(uint64_t sender_id, uint64_t target_id,
                      const std::string& content, int message_type);
    bool save_group_message(uint64_t group_id, uint64_t sender_id,
                            const std::string& content);
    bool save_offline_message(uint64_t user_id,
                              uint64_t sender_id,
                              const std::string& sender_name,
                              const std::string& content);
    std::vector<QueryResult::MessageHistory> get_history(uint64_t user_id,
                                                          uint64_t peer_id,
                                                          int limit = 50);
    std::vector<QueryResult::MessageHistory> get_group_history(uint64_t group_id,
                                                                int limit = 50);
    std::vector<QueryResult::MessageHistory> get_offline_messages(uint64_t user_id);
    bool mark_read(uint64_t message_id);

    // 文件模块
    uint64_t save_file_metadata(const std::string& file_name,
                                uint64_t file_size,
                                const std::string& file_path,
                                uint64_t uploader_id,
                                uint64_t target_id);
    QueryResult::FileInfo get_file_info(uint64_t file_id);

    // 分片上传状态追踪 (Redis)
    bool record_file_chunk(uint64_t uploader_id, const std::string& file_name,
                           uint64_t file_size, uint32_t chunk_seq);
    std::vector<uint32_t> get_received_chunks(uint64_t uploader_id,
                                               const std::string& file_name,
                                               uint64_t file_size);
    bool clear_file_chunks(uint64_t uploader_id, const std::string& file_name,
                           uint64_t file_size);

    // 在线状态
    void set_online(uint64_t user_id);
    void set_offline(uint64_t user_id);
    bool is_online(uint64_t user_id);

    // 健康检查
    bool is_connected() const;

private:
    // 密码工具 
    static std::string generate_salt();
    static std::string hash_password(const std::string& password,
                                     const std::string& salt);

    // 会话令牌生成
    static std::string generate_token();

    // Redis 辅助方法
    bool redis_set(const std::string& key, const std::string& value);
    bool redis_set_ex(const std::string& key, const std::string& value, int seconds);
    std::string redis_get(const std::string& key);
    bool redis_del(const std::string& key);
    bool redis_hset(const std::string& key, const std::string& field,
                    const std::string& value);
    std::string redis_hget(const std::string& key, const std::string& field);
    bool redis_sadd(const std::string& key, const std::string& member);
    bool redis_srem(const std::string& key, const std::string& member);
    bool redis_sismember(const std::string& key, const std::string& member);
    std::vector<std::string> redis_smembers(const std::string& key);
    bool redis_lpush(const std::string& key, const std::string& value);
    bool redis_expire(const std::string& key, int seconds);
    std::vector<std::string> redis_lrange(const std::string& key, int start, int stop);
    bool redis_del_key(const std::string& key);
    bool redis_reconnect();

    // SQL 工具
    std::string escape_string(MYSQL* conn, const std::string& str);

    // MySQL 连接池 + Redis 上下文
    std::unique_ptr<MySQLConnectionPool> mysql_pool_;
    redisContext* redis_ctx_ = nullptr;
    mutable std::mutex redis_mutex_;

    std::string redis_host_ = "127.0.0.1";
    int redis_port_ = 6379;
};

} // namespace chatroom
