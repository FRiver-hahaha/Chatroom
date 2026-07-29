#include "StorageManager.hpp"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace chatroom {

static std::string bin_to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    return oss.str();
}

static std::string now_str() {
    return std::to_string(std::time(nullptr));
}

MySQLConnectionPool::MySQLConnectionPool(size_t pool_size) : pool_size_(pool_size) {}
MySQLConnectionPool::~MySQLConnectionPool() { close_all(); }

bool MySQLConnectionPool::init(const std::string& host, const std::string& user,
                                const std::string& password, const std::string& database,
                                unsigned int port) {
    conn_info_ = {host, user, password, database, port};
    for (size_t i = 0; i < pool_size_; ++i) {
        MYSQL* conn = create_connection();
        if (!conn) { close_all(); return false; }
        pool_.push(conn);
    }
    initialized_ = true;
    std::cout << "[MySQLPool] Initialized with " << pool_size_ << " connections" << std::endl;
    return true;
}

MYSQL* MySQLConnectionPool::create_connection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) return nullptr;
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (!mysql_real_connect(conn, conn_info_.host.c_str(), conn_info_.user.c_str(),
                            conn_info_.password.c_str(), conn_info_.database.c_str(),
                            conn_info_.port, nullptr, 0)) {
        std::cerr << "[MySQLPool] Connection failed: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

MYSQL* MySQLConnectionPool::acquire() {// 获取一个连接
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pool_.empty(); });
    MYSQL* conn = pool_.front(); pool_.pop();
    if (mysql_ping(conn) != 0) {
        mysql_close(conn);
        conn = create_connection();
    }
    return conn;
}

void MySQLConnectionPool::release(MYSQL* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}

void MySQLConnectionPool::close_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) { mysql_close(pool_.front()); pool_.pop(); }
    initialized_ = false;
}

StorageManager::StorageManager() = default;
StorageManager::~StorageManager() { disconnect(); }

bool StorageManager::connect(const std::string& mysql_host, const std::string& mysql_user,
                              const std::string& mysql_password, const std::string& mysql_database,
                              int redis_port, const std::string& redis_host) {
    redis_host_ = redis_host;
    redis_port_ = redis_port;
    mysql_pool_ = std::make_unique<MySQLConnectionPool>(8);// 初始化
    if (!mysql_pool_->init(mysql_host, mysql_user, mysql_password, mysql_database)) {
        std::cerr << "[StorageManager] MySQL pool init failed" << std::endl;
        return false;
    }
    redis_ctx_ = redisConnect(redis_host_.c_str(), redis_port_);
    if (!redis_ctx_ || redis_ctx_->err) {
        std::cerr << "[StorageManager] Redis connection failed: "
                  << (redis_ctx_ ? redis_ctx_->errstr : "null context") << std::endl;
        if (redis_ctx_) { redisFree(redis_ctx_); redis_ctx_ = nullptr; }
        return false;
    }
    std::cout << "[StorageManager] Connected to MySQL and Redis" << std::endl;
    return true;
}

void StorageManager::disconnect() {
    if (redis_ctx_) { redisFree(redis_ctx_); redis_ctx_ = nullptr; }
    if (mysql_pool_) { mysql_pool_->close_all(); mysql_pool_.reset(); }
    std::cout << "[StorageManager] Disconnected" << std::endl;
}

bool StorageManager::is_connected() const {
    return redis_ctx_ != nullptr && mysql_pool_ != nullptr;
}

// SQL 转义
std::string StorageManager::escape_string(MYSQL* conn, const std::string& str) {
    if (str.empty()) return "";
    std::vector<char> buf(str.size() * 2 + 1);
    mysql_real_escape_string(conn, buf.data(), str.c_str(), str.size());
    return std::string(buf.data());
}

std::string StorageManager::generate_salt() {
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    return bin_to_hex(buf, sizeof(buf));
}

std::string StorageManager::hash_password(const std::string& password, const std::string& salt) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    std::string combined = salt + password;
    SHA256(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);
    return bin_to_hex(hash, SHA256_DIGEST_LENGTH);
}

std::string StorageManager::generate_token() {
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) oss << std::setw(2) << static_cast<int>(buf[i]);
    return oss.str();
}

bool StorageManager::redis_reconnect() {
    if (redis_ctx_) redisFree(redis_ctx_);
    redis_ctx_ = redisConnect(redis_host_.c_str(), redis_port_);
    return redis_ctx_ && !redis_ctx_->err;
}

#define REDIS_CMD(func, fmt, ...)                                               \
    do {                                                                         \
        std::lock_guard<std::mutex> lock(redis_mutex_);                          \
        if (!redis_ctx_) { ok = false; break; }                                  \
        auto* _r = reinterpret_cast<redisReply*>(                                 \
            redisCommand(redis_ctx_, fmt, ##__VA_ARGS__));                       \
        if (!_r) {                                                               \
            if (redis_reconnect())                                                \
                _r = reinterpret_cast<redisReply*>(                               \
                    redisCommand(redis_ctx_, fmt, ##__VA_ARGS__));               \
            if (!_r) { ok = false; break; }                                      \
        }                                                                        \
        ok = (_r->type != REDIS_REPLY_ERROR);                                    \
        if (!ok) std::cerr << "[Redis] " << _r->str << std::endl;               \
        freeReplyObject(_r);                                                     \
    } while (0)

bool StorageManager::redis_set(const std::string& key, const std::string& value) {
    bool ok = false; REDIS_CMD(, "SET %s %s", key.c_str(), value.c_str()); return ok;
}
bool StorageManager::redis_set_ex(const std::string& key, const std::string& value, int sec) {
    bool ok = false; REDIS_CMD(, "SETEX %s %d %s", key.c_str(), sec, value.c_str()); return ok;
}
bool StorageManager::redis_del(const std::string& key) {
    bool ok = false; REDIS_CMD(, "DEL %s", key.c_str()); return ok;
}
bool StorageManager::redis_hset(const std::string& key, const std::string& f, const std::string& v) {
    bool ok = false; REDIS_CMD(, "HSET %s %s %s", key.c_str(), f.c_str(), v.c_str()); return ok;
}
bool StorageManager::redis_sadd(const std::string& key, const std::string& m) {
    bool ok = false; REDIS_CMD(, "SADD %s %s", key.c_str(), m.c_str()); return ok;
}
bool StorageManager::redis_srem(const std::string& key, const std::string& m) {
    bool ok = false; REDIS_CMD(, "SREM %s %s", key.c_str(), m.c_str()); return ok;
}
bool StorageManager::redis_lpush(const std::string& key, const std::string& v) {
    bool ok = false; REDIS_CMD(, "LPUSH %s %s", key.c_str(), v.c_str()); return ok;
}
bool StorageManager::redis_expire(const std::string& key, int sec) {
    bool ok = false; REDIS_CMD(, "EXPIRE %s %d", key.c_str(), sec); return ok;
}
bool StorageManager::redis_del_key(const std::string& key) { return redis_del(key); }

std::string StorageManager::redis_get(const std::string& key) {
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return "";
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "GET %s", key.c_str()));
    if (!r || r->type == REDIS_REPLY_NIL || r->type == REDIS_REPLY_ERROR) {
        if (r) freeReplyObject(r);
        return "";
    }
    std::string res(r->str, r->len);
    freeReplyObject(r);
    return res;
}
std::string StorageManager::redis_hget(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return "";
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "HGET %s %s", key.c_str(), field.c_str()));
    if (!r || r->type == REDIS_REPLY_NIL || r->type == REDIS_REPLY_ERROR) {
        if (r) freeReplyObject(r);
        return "";
    }
    std::string res(r->str, r->len); freeReplyObject(r); return res;
}
bool StorageManager::redis_sismember(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return false;
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "SISMEMBER %s %s", key.c_str(), member.c_str()));
    if (!r || r->type != REDIS_REPLY_INTEGER) { if (r) freeReplyObject(r); return false; }
    bool ok = (r->integer == 1); freeReplyObject(r); return ok;
}
std::vector<std::string> StorageManager::redis_lrange(const std::string& key, int start, int stop) {
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return result;
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "LRANGE %s %d %d", key.c_str(), start, stop));
    if (!r || r->type != REDIS_REPLY_ARRAY) { if (r) freeReplyObject(r); return result; }
    for (size_t i = 0; i < r->elements; ++i)
        result.emplace_back(r->element[i]->str, r->element[i]->len);
    freeReplyObject(r);
    return result;
}

bool StorageManager::user_exists(const std::string& username) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string e = escape_string(conn, username);
    std::string q = "SELECT COUNT(*) FROM users WHERE username='" + e + "'";
    bool exists = false;
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        exists = (row && std::stoi(row[0]) > 0);
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return exists;
}

QueryResult StorageManager::create_user(const std::string& username,
                                          const std::string& password,
                                          const std::string& nickname) {
    QueryResult result;
    if (user_exists(username)) {
        result.success = false; result.error_message = "用户名已存在"; return result;
    }
    std::string salt = generate_salt();
    std::string hash = hash_password(password, salt);
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) { result.success = false; result.error_message = "数据库连接失败"; return result; }
    std::string eu = escape_string(conn, username);
    std::string en = escape_string(conn, nickname);
    std::string q = "INSERT INTO users (username, password_hash, salt, nickname) VALUES ('"
                   + eu + "', '" + hash + "', '" + salt + "', '" + en + "')";
    if (mysql_query(conn, q.c_str()) != 0) {
        result.success = false;
        result.error_message = std::string("创建用户失败: ") + mysql_error(conn);
        mysql_pool_->release(conn); return result;
    }
    uint64_t user_id = mysql_insert_id(conn);
    mysql_pool_->release(conn);
    result.success = true; result.user_id = user_id;
    result.username = username; result.nickname = nickname;
    result.token = generate_token();
    std::string sk = "chatroom:session:" + std::to_string(user_id);
    redis_hset(sk, "username", username);
    redis_hset(sk, "token", result.token);
    redis_hset(sk, "nickname", nickname);
    redis_expire(sk, 86400);
    redis_set_ex("chatroom:token:" + result.token, std::to_string(user_id), 86400);
    return result;
}

QueryResult StorageManager::get_user_by_username(const std::string& username) {
    QueryResult result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) { result.success = false; result.error_message = "数据库连接失败"; return result; }
    std::string e = escape_string(conn, username);
    std::string q = "SELECT id, username, password_hash, salt, nickname, status "
                    "FROM users WHERE username='" + e + "'";
    if (mysql_query(conn, q.c_str()) != 0) {
        result.success = false; result.error_message = "查询用户失败";
        mysql_pool_->release(conn); return result;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        mysql_pool_->release(conn);
        result.success = false; result.error_message = "用户不存在"; return result;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    result.success = true;
    result.user_id = std::stoull(row[0]);
    result.username = row[1] ? row[1] : "";
    result.nickname = row[4] ? row[4] : "";
    result.user_status = row[5] ? std::stoi(row[5]) : 1;
    result.is_online = is_online(result.user_id);
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

QueryResult StorageManager::get_user_by_id(uint64_t user_id) {
    QueryResult result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) { result.success = false; result.error_message = "数据库连接失败"; return result; }
    std::string q = "SELECT id, username, nickname, status FROM users WHERE id=" + std::to_string(user_id);
    if (mysql_query(conn, q.c_str()) != 0) {
        result.success = false; result.error_message = "查询用户失败";
        mysql_pool_->release(conn); return result;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        mysql_pool_->release(conn);
        result.success = false; result.error_message = "用户不存在"; return result;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    result.success = true;
    result.user_id = std::stoull(row[0]);
    result.username = row[1] ? row[1] : "";
    result.nickname = row[2] ? row[2] : "";
    result.user_status = row[3] ? std::stoi(row[3]) : 1;
    result.is_online = is_online(result.user_id);
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

bool StorageManager::verify_password(const std::string& username, const std::string& password) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string e = escape_string(conn, username);
    std::string q = "SELECT password_hash, salt FROM users WHERE username='" + e + "'";
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return false; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        mysql_pool_->release(conn); return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    std::string stored_hash(row[0] ? row[0] : "");
    std::string salt(row[1] ? row[1] : "");
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return hash_password(password, salt) == stored_hash;
}

bool StorageManager::update_password(uint64_t user_id, const std::string& new_password) {
    std::string salt = generate_salt();
    std::string hash = hash_password(new_password, salt);
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE users SET password_hash='" + hash + "', salt='" + salt
                   + "' WHERE id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

std::string StorageManager::create_session(uint64_t user_id, const std::string& username) {
    std::string token = generate_token();
    std::string sk = "chatroom:session:" + std::to_string(user_id);
    redis_hset(sk, "username", username);
    redis_hset(sk, "token", token);
    redis_hset(sk, "login_time", now_str());
    redis_expire(sk, 86400);
    redis_set_ex("chatroom:token:" + token, std::to_string(user_id), 86400);
    return token;
}

bool StorageManager::verify_token(const std::string& token) {
    return !redis_get("chatroom:token:" + token).empty();
}

bool StorageManager::delete_user(uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;

    // 手动清理 messages 表（无外键约束）
    std::string del_msg = "DELETE FROM messages WHERE sender_id=" + std::to_string(user_id);
    mysql_query(conn, del_msg.c_str());

    // DELETE FROM users -- CASCADE 自动清理 friendships, group_members, chat_groups, files
    std::string q = "DELETE FROM users WHERE id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);

    // 清理 Redis
    redis_del("chatroom:session:" + std::to_string(user_id));
    redis_srem("chatroom:online", std::to_string(user_id));
    redis_del("chatroom:user:" + std::to_string(user_id) + ":friends");
    redis_del("chatroom:user:" + std::to_string(user_id) + ":groups");
    redis_del("chatroom:offline_msg:" + std::to_string(user_id));

    return ok;
}

bool StorageManager::clear_session(uint64_t user_id) {
    std::string sk = "chatroom:session:" + std::to_string(user_id);
    std::string token = redis_hget(sk, "token");
    if (!token.empty()) redis_del("chatroom:token:" + token);
    redis_del(sk);
    return true;
}

bool StorageManager::add_friend(uint64_t user_id, uint64_t friend_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q1 = "INSERT IGNORE INTO friendships (user_id, friend_id) VALUES ("
                    + std::to_string(user_id) + ", " + std::to_string(friend_id) + ")";
    std::string q2 = "INSERT IGNORE INTO friendships (user_id, friend_id) VALUES ("
                    + std::to_string(friend_id) + ", " + std::to_string(user_id) + ")";
    bool ok = (mysql_query(conn, q1.c_str()) == 0 && mysql_query(conn, q2.c_str()) == 0);
    mysql_pool_->release(conn);
    redis_del("chatroom:user:" + std::to_string(user_id) + ":friends");
    redis_del("chatroom:user:" + std::to_string(friend_id) + ":friends");
    return ok;
}

bool StorageManager::remove_friend(uint64_t user_id, uint64_t friend_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q1 = "DELETE FROM friendships WHERE user_id=" + std::to_string(user_id)
                    + " AND friend_id=" + std::to_string(friend_id);
    std::string q2 = "DELETE FROM friendships WHERE user_id=" + std::to_string(friend_id)
                    + " AND friend_id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q1.c_str()) == 0);
    mysql_query(conn, q2.c_str());
    mysql_pool_->release(conn);
    redis_del("chatroom:user:" + std::to_string(user_id) + ":friends");
    redis_del("chatroom:user:" + std::to_string(friend_id) + ":friends");
    return ok;
}

bool StorageManager::is_friend(uint64_t user_id, uint64_t friend_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "SELECT COUNT(*) FROM friendships WHERE user_id="
                   + std::to_string(user_id) + " AND friend_id=" + std::to_string(friend_id);
    bool result = false;
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        result = (row && std::stoi(row[0]) > 0);
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return result;
}

bool StorageManager::block_friend(uint64_t user_id, uint64_t friend_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE friendships SET is_blocked=TRUE WHERE user_id="
                   + std::to_string(user_id) + " AND friend_id=" + std::to_string(friend_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::unblock_friend(uint64_t user_id, uint64_t friend_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE friendships SET is_blocked=FALSE WHERE user_id="
                   + std::to_string(user_id) + " AND friend_id=" + std::to_string(friend_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::is_blocked_by(uint64_t user_id, uint64_t friend_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "SELECT COUNT(*) FROM friendships WHERE user_id="
                   + std::to_string(user_id) + " AND friend_id=" + std::to_string(friend_id)
                   + " AND is_blocked=TRUE";
    bool result = false;
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        result = (row && std::stoi(row[0]) > 0);
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return result;
}

std::vector<QueryResult::FriendInfo> StorageManager::get_friends(uint64_t user_id) {
    std::vector<QueryResult::FriendInfo> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    std::string q = "SELECT u.id, u.username, u.nickname, f.is_blocked, UNIX_TIMESTAMP(f.created_at) "
                    "FROM friendships f JOIN users u ON f.friend_id = u.id "
                    "WHERE f.user_id=" + std::to_string(user_id);
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return result; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return result; }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        QueryResult::FriendInfo info;
        info.user_id   = std::stoull(row[0]);
        info.username  = row[1] ? row[1] : "";
        info.nickname  = row[2] ? row[2] : "";
        info.is_blocked = row[3] ? (std::stoi(row[3]) != 0) : false;
        info.add_time  = row[4] ? std::stoull(row[4]) : 0;
        info.is_online = is_online(info.user_id);
        result.push_back(info);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

uint64_t StorageManager::create_group(const std::string& group_name,
                                        const std::string& description,
                                        uint64_t owner_id, bool is_public) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return 0;
    std::string eg = escape_string(conn, group_name);
    std::string ed = escape_string(conn, description);
    std::string q = "INSERT INTO chat_groups (group_name, description, owner_id, is_public) VALUES ('"
                   + eg + "', '" + ed + "', " + std::to_string(owner_id)
                   + ", " + (is_public ? "TRUE" : "FALSE") + ")";
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return 0; }
    uint64_t group_id = mysql_insert_id(conn);
    std::string mq = "INSERT INTO group_members (group_id, user_id, role) VALUES ("
                    + std::to_string(group_id) + ", " + std::to_string(owner_id) + ", 'owner')";
    mysql_query(conn, mq.c_str());
    mysql_pool_->release(conn);
    redis_del("chatroom:user:" + std::to_string(owner_id) + ":groups");
    return group_id;
}

bool StorageManager::dismiss_group(uint64_t group_id, uint64_t requester_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ck = "SELECT owner_id FROM chat_groups WHERE id=" + std::to_string(group_id);
    if (mysql_query(conn, ck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row || std::stoull(row[0]) != requester_id) {
            mysql_free_result(res); mysql_pool_->release(conn); return false;
        }
        mysql_free_result(res);
    }
    std::string q = "DELETE FROM chat_groups WHERE id=" + std::to_string(group_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    return ok;
}

bool StorageManager::join_group(uint64_t group_id, uint64_t user_id) {
    if (is_group_member(group_id, user_id)) return false;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "INSERT INTO group_members (group_id, user_id, role) VALUES ("
                   + std::to_string(group_id) + ", " + std::to_string(user_id) + ", 'member')";
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    if (ok) {
        std::string up = "UPDATE chat_groups SET member_count = member_count + 1 WHERE id="
                        + std::to_string(group_id);
        mysql_query(conn, up.c_str());
    }
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    redis_del("chatroom:user:" + std::to_string(user_id) + ":groups");
    return ok;
}

bool StorageManager::quit_group(uint64_t group_id, uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "DELETE FROM group_members WHERE group_id=" + std::to_string(group_id)
                   + " AND user_id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    if (ok) {
        std::string up = "UPDATE chat_groups SET member_count = GREATEST(member_count - 1, 0) WHERE id="
                        + std::to_string(group_id);
        mysql_query(conn, up.c_str());

        // 检查群组是否为空，自动删除
        std::string check = "SELECT member_count FROM chat_groups WHERE id=" + std::to_string(group_id);
        if (mysql_query(conn, check.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && std::stoll(row[0]) <= 0) {
                std::string del = "DELETE FROM chat_groups WHERE id=" + std::to_string(group_id);
                mysql_query(conn, del.c_str());
            }
            mysql_free_result(res);
        }
    }
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    redis_del("chatroom:user:" + std::to_string(user_id) + ":groups");
    return ok;
}

bool StorageManager::add_admin(uint64_t group_id, uint64_t owner_id, uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ck = "SELECT owner_id FROM chat_groups WHERE id=" + std::to_string(group_id);
    bool is_owner = false;
    if (mysql_query(conn, ck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        is_owner = (row && std::stoull(row[0]) == owner_id);
        mysql_free_result(res);
    }
    if (!is_owner) { mysql_pool_->release(conn); return false; }
    std::string q = "UPDATE group_members SET role='admin' WHERE group_id="
                   + std::to_string(group_id) + " AND user_id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    return ok;
}

bool StorageManager::remove_admin(uint64_t group_id, uint64_t owner_id, uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ck = "SELECT owner_id FROM chat_groups WHERE id=" + std::to_string(group_id);
    bool is_owner = false;
    if (mysql_query(conn, ck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        is_owner = (row && std::stoull(row[0]) == owner_id);
        mysql_free_result(res);
    }
    if (!is_owner) { mysql_pool_->release(conn); return false; }
    std::string q = "UPDATE group_members SET role='member' WHERE group_id="
                   + std::to_string(group_id) + " AND user_id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    return ok;
}

bool StorageManager::approve_join(uint64_t group_id, uint64_t admin_id, uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ck = "SELECT role FROM group_members WHERE group_id=" + std::to_string(group_id)
                    + " AND user_id=" + std::to_string(admin_id);
    bool can = false;
    if (mysql_query(conn, ck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) { std::string r(row[0]); can = (r == "owner" || r == "admin"); }
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return can ? join_group(group_id, user_id) : false;
}

bool StorageManager::remove_member(uint64_t group_id, uint64_t admin_id, uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ck = "SELECT role FROM group_members WHERE group_id=" + std::to_string(group_id)
                    + " AND user_id=" + std::to_string(admin_id);
    bool can = false;
    if (mysql_query(conn, ck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) { std::string r(row[0]); can = (r == "owner" || r == "admin"); }
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return can ? quit_group(group_id, user_id) : false;
}

bool StorageManager::is_group_member(uint64_t group_id, uint64_t user_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "SELECT COUNT(*) FROM group_members WHERE group_id=" + std::to_string(group_id)
                   + " AND user_id=" + std::to_string(user_id);
    bool result = false;
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        result = (row && std::stoi(row[0]) > 0);
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return result;
}

QueryResult::GroupInfo StorageManager::get_group_info(uint64_t group_id) {
    QueryResult::GroupInfo info{};
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return info;
    std::string q = "SELECT id, group_name, description, owner_id, member_count "
                    "FROM chat_groups WHERE id=" + std::to_string(group_id);
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            info.group_id    = std::stoull(row[0]);
            info.group_name  = row[1] ? row[1] : "";
            info.description = row[2] ? row[2] : "";
            info.owner_id    = std::stoull(row[3]);
            info.member_count = std::stoull(row[4]);
        }
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return info;
}

std::vector<QueryResult::GroupInfo> StorageManager::get_user_groups(uint64_t user_id) {
    std::vector<QueryResult::GroupInfo> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    std::string q = "SELECT g.id, g.group_name, g.description, g.owner_id, g.member_count "
                    "FROM chat_groups g JOIN group_members gm ON g.id = gm.group_id "
                    "WHERE gm.user_id=" + std::to_string(user_id);
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return result; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return result; }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        QueryResult::GroupInfo info;
        info.group_id    = std::stoull(row[0]);
        info.group_name  = row[1] ? row[1] : "";
        info.description = row[2] ? row[2] : "";
        info.owner_id    = std::stoull(row[3]);
        info.member_count = std::stoull(row[4]);
        info.is_member   = true;
        result.push_back(info);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

std::vector<QueryResult::GroupMember> StorageManager::get_group_members(uint64_t group_id) {
    std::vector<QueryResult::GroupMember> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    std::string q = "SELECT u.id, u.username, u.nickname, gm.role, UNIX_TIMESTAMP(gm.joined_at) "
                    "FROM group_members gm JOIN users u ON gm.user_id = u.id "
                    "WHERE gm.group_id=" + std::to_string(group_id);
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return result; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return result; }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        QueryResult::GroupMember member;
        member.user_id   = std::stoull(row[0]);
        member.username  = row[1] ? row[1] : "";
        member.nickname  = row[2] ? row[2] : "";
        member.role      = row[3] ? row[3] : "member";
        member.join_time = row[4] ? std::stoull(row[4]) : 0;
        result.push_back(member);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

bool StorageManager::save_message(uint64_t sender_id, uint64_t target_id,
                                    const std::string& content, int message_type) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ec = escape_string(conn, content);
    std::string q = "INSERT INTO messages (sender_id, target_id, message_type, body) VALUES ("
                   + std::to_string(sender_id) + ", " + std::to_string(target_id) + ", "
                   + std::to_string(message_type) + ", '" + ec + "')";
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::save_group_message(uint64_t group_id, uint64_t sender_id,
                                          const std::string& content) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ec = escape_string(conn, content);
    std::string q = "INSERT INTO messages (sender_id, group_id, message_type, body) VALUES ("
                   + std::to_string(sender_id) + ", " + std::to_string(group_id)
                   + ", 302, '" + ec + "')";
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::save_offline_message(uint64_t user_id, uint64_t sender_id,
                                            const std::string& sender_name,
                                            const std::string& content) {
    std::string key = "chatroom:offline_msg:" + std::to_string(user_id);
    std::string msg = std::to_string(sender_id) + "|" + sender_name + "|"
                     + content + "|" + now_str();
    bool ok = redis_lpush(key, msg);
    if (ok) redis_expire(key, 604800);
    return ok;
}

std::vector<QueryResult::MessageHistory> StorageManager::get_history(
    uint64_t user_id, uint64_t peer_id, int limit) {
    std::vector<QueryResult::MessageHistory> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    auto uid = std::to_string(user_id), pid = std::to_string(peer_id);
    std::string q = "SELECT m.id, m.sender_id, u.username, m.body, UNIX_TIMESTAMP(m.created_at), m.is_read "
                    "FROM messages m JOIN users u ON m.sender_id = u.id "
                    "WHERE ((m.sender_id=" + uid + " AND m.target_id=" + pid + ") "
                    "OR (m.sender_id=" + pid + " AND m.target_id=" + uid + ")) "
                    "AND m.group_id IS NULL ORDER BY m.created_at DESC LIMIT " + std::to_string(limit);
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return result; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return result; }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        QueryResult::MessageHistory item;
        item.message_id = std::stoull(row[0]); item.sender_id = std::stoull(row[1]);
        item.sender_name = row[2] ? row[2] : ""; item.content = row[3] ? row[3] : "";
        item.timestamp = row[4] ? std::stoull(row[4]) : 0;
        item.is_read   = row[5] ? (std::stoi(row[5]) != 0) : false;
        result.push_back(item);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<QueryResult::MessageHistory> StorageManager::get_group_history(uint64_t group_id, int limit) {
    std::vector<QueryResult::MessageHistory> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    std::string q = "SELECT m.id, m.sender_id, u.username, m.body, UNIX_TIMESTAMP(m.created_at), m.is_read "
                    "FROM messages m JOIN users u ON m.sender_id = u.id "
                    "WHERE m.group_id=" + std::to_string(group_id)
                    + " ORDER BY m.created_at DESC LIMIT " + std::to_string(limit);
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return result; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return result; }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        QueryResult::MessageHistory item;
        item.message_id = std::stoull(row[0]); item.sender_id = std::stoull(row[1]);
        item.sender_name = row[2] ? row[2] : ""; item.content = row[3] ? row[3] : "";
        item.timestamp = row[4] ? std::stoull(row[4]) : 0;
        item.is_read   = row[5] ? (std::stoi(row[5]) != 0) : false;
        result.push_back(item);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<QueryResult::MessageHistory> StorageManager::get_offline_messages(uint64_t user_id) {
    std::vector<QueryResult::MessageHistory> result;
    auto msgs = redis_lrange("chatroom:offline_msg:" + std::to_string(user_id), 0, -1);
    for (auto& msg : msgs) {
        size_t p1 = msg.find('|'), p2 = msg.find('|', p1 + 1), p3 = msg.find('|', p2 + 1);
        if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
            QueryResult::MessageHistory item;
            item.sender_id   = std::stoull(msg.substr(0, p1));
            item.sender_name = msg.substr(p1 + 1, p2 - p1 - 1);
            item.content     = msg.substr(p2 + 1, p3 - p2 - 1);
            item.timestamp   = std::stoull(msg.substr(p3 + 1));
            result.push_back(item);
        }
    }
    redis_del_key("chatroom:offline_msg:" + std::to_string(user_id));
    return result;
}

bool StorageManager::mark_read(uint64_t message_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE messages SET is_read=TRUE WHERE id=" + std::to_string(message_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

uint64_t StorageManager::save_file_metadata(const std::string& file_name, uint64_t file_size,
                                              const std::string& file_path, uint64_t uploader_id,
                                              uint64_t target_id) {
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return 0;
    std::string ef = escape_string(conn, file_name);
    std::string ep = escape_string(conn, file_path);
    std::string q = "INSERT INTO files (file_name, file_size, file_path, uploader_id, target_id) VALUES ('"
                   + ef + "', " + std::to_string(file_size) + ", '" + ep + "', "
                   + std::to_string(uploader_id) + ", " + std::to_string(target_id) + ")";
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return 0; }
    uint64_t file_id = mysql_insert_id(conn);
    mysql_pool_->release(conn);
    return file_id;
}

QueryResult::FileInfo StorageManager::get_file_info(uint64_t file_id) {
    QueryResult::FileInfo info{};
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return info;
    std::string q = "SELECT f.id, f.file_name, f.file_size, f.file_path, f.uploader_id, "
                    "u.username, UNIX_TIMESTAMP(f.created_at) "
                    "FROM files f JOIN users u ON f.uploader_id = u.id WHERE f.id=" + std::to_string(file_id);
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            info.file_id    = std::stoull(row[0]); info.file_name = row[1] ? row[1] : "";
            info.file_size  = std::stoull(row[2]); info.file_path = row[3] ? row[3] : "";
            info.sender_id  = std::stoull(row[4]); info.sender_name = row[5] ? row[5] : "";
            info.timestamp  = row[6] ? std::stoull(row[6]) : 0;
        }
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return info;
}

void StorageManager::set_online(uint64_t user_id)   { redis_sadd("chatroom:online", std::to_string(user_id)); }
void StorageManager::set_offline(uint64_t user_id)  { redis_srem("chatroom:online", std::to_string(user_id)); }
bool StorageManager::is_online(uint64_t user_id)     { return redis_sismember("chatroom:online", std::to_string(user_id)); }

} // namespace chatroom
