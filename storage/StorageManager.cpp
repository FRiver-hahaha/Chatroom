#include "StorageManager.hpp"
#include <glog/logging.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>
#include <cerrno>

namespace chatroom {

static std::string bin_to_hex(const unsigned char* data, size_t len) {// 转换为16进制，方便数据库查询
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    return oss.str();
}

static std::string now_str() {// 获取当前时间
    return std::to_string(std::time(nullptr));
}

static std::string xfer_sender_key(uint64_t transfer_id) {// 记录已经上传好的分片
    return "chatroom:transfer:" + std::to_string(transfer_id) + ":sender_chunks";
}
static std::string xfer_receiver_key(uint64_t transfer_id) {// 记录已经下载好的分片
    return "chatroom:transfer:" + std::to_string(transfer_id) + ":receiver_chunks";
}
static std::string xfer_pending_key(uint64_t user_id) {// 记录用户待接收的分片
    return "chatroom:transfer:pending:" + std::to_string(user_id);
}
static const char* FILE_STORAGE_BASE = "/tmp/chatroom_files";

static std::string xfer_chunk_path(uint64_t transfer_id, uint32_t chunk_seq) {// 分片文件路径
    return std::string(FILE_STORAGE_BASE) + "/transfer_" + std::to_string(transfer_id)
           + "/chunk_" + std::to_string(chunk_seq);
}

// 验证码 Redis key 构造（scene: register/reset，channel: email/phone）
static std::string vcode_key(const std::string& scene, const std::string& channel,
                             const std::string& target) {
    return "chatroom:vcode:" + scene + ":" + channel + ":" + target;
}
static std::string vcode_rate_key(const std::string& scene, const std::string& channel,
                                  const std::string& target) {
    return "chatroom:vcode:rate:" + scene + ":" + channel + ":" + target;
}
static std::string vcode_try_key(const std::string& scene, const std::string& channel,
                                 const std::string& target) {
    return "chatroom:vcode:try:" + scene + ":" + channel + ":" + target;
}

MySQLConnectionPool::MySQLConnectionPool(size_t pool_size) : pool_size_(pool_size) {} // 构造连接池，仅记录连接数量
MySQLConnectionPool::~MySQLConnectionPool() { close_all(); } // 析构时关闭所有连接

bool MySQLConnectionPool::init(const std::string& host, const std::string& user,
                                const std::string& password, const std::string& database,
                                unsigned int port) {// 初始化数据库连接池
    conn_info_ = {host, user, password, database, port};
    for (size_t i = 0; i < pool_size_; ++i) {
        MYSQL* conn = create_connection();
        if (!conn) { close_all(); return false; }
        pool_.push(conn);
    }
    initialized_ = true;
    LOG(INFO) << "[MySQLPool] Initialized with " << pool_size_ << " connections" ;
    return true;
}

MYSQL* MySQLConnectionPool::create_connection() {// 创建一个连接
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) return nullptr;
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (!mysql_real_connect(conn, conn_info_.host.c_str(), conn_info_.user.c_str(),
                            conn_info_.password.c_str(), conn_info_.database.c_str(),
                            conn_info_.port, nullptr, 0)) {
        LOG(ERROR) << "[MySQLPool] Connection failed: " << mysql_error(conn) ;
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

void MySQLConnectionPool::release(MYSQL* conn) {// 归还一个连接
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}

void MySQLConnectionPool::close_all() {// 关闭连接
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) { mysql_close(pool_.front()); pool_.pop(); }
    initialized_ = false;
}

StorageManager::StorageManager() = default; // 默认构造
StorageManager::~StorageManager() { disconnect(); } // 析构时断开连接

bool StorageManager::connect(const std::string& mysql_host, const std::string& mysql_user,
                              const std::string& mysql_password, const std::string& mysql_database,
                              int redis_port, const std::string& redis_host) {// 连接到数据库上
    redis_host_ = redis_host;
    redis_port_ = redis_port;
    mysql_pool_ = std::make_unique<MySQLConnectionPool>(8);// 初始化
    if (!mysql_pool_->init(mysql_host, mysql_user, mysql_password, mysql_database)) {
        LOG(ERROR) << "[StorageManager] MySQL pool init failed" ;
        return false;
    }
    redis_ctx_ = redisConnect(redis_host_.c_str(), redis_port_);
    if (!redis_ctx_ || redis_ctx_->err) {
        LOG(ERROR) << "[StorageManager] Redis connection failed: "
                  << (redis_ctx_ ? redis_ctx_->errstr : "null context") ;
        if (redis_ctx_) { redisFree(redis_ctx_); redis_ctx_ = nullptr; }
        return false;
    }
    redis_del("chatroom:online");
    {
        MYSQL* conn = mysql_pool_->acquire();
        if (conn) {
            const char* create_sql = R"SQL(
                CREATE TABLE IF NOT EXISTS file_transfers (
                    transfer_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                    sender_id BIGINT UNSIGNED NOT NULL,
                    receiver_id BIGINT UNSIGNED NOT NULL,
                    file_name VARCHAR(255) NOT NULL,
                    file_size BIGINT UNSIGNED NOT NULL,
                    total_chunks INT UNSIGNED NOT NULL DEFAULT 0,
                    file_hash VARCHAR(64) DEFAULT '',
                    status ENUM('sending','completed','rejected') DEFAULT 'sending',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                ) ENGINE=InnoDB
            )SQL";
            if (mysql_query(conn, create_sql) != 0) {
                LOG(ERROR) << "[StorageManager] CREATE file_transfers failed: "
                          << mysql_error(conn) ;
            }
            mysql_query(conn, "ALTER TABLE file_transfers ADD COLUMN IF NOT EXISTS "
                             "file_hash VARCHAR(64) DEFAULT ''");
            mysql_pool_->release(conn);
        }
    }

    LOG(INFO) << "[StorageManager] Connected to MySQL and Redis" ;
    return true;
}

void StorageManager::disconnect() {// 取消链接
    if (redis_ctx_) { redisFree(redis_ctx_); redis_ctx_ = nullptr; }
    if (mysql_pool_) { mysql_pool_->close_all(); mysql_pool_.reset(); }
    LOG(INFO) << "[StorageManager] Disconnected" ;
}

bool StorageManager::is_connected() const {// 检查是否连接
    return redis_ctx_ != nullptr && mysql_pool_ != nullptr;
}

std::string StorageManager::escape_string(MYSQL* conn, const std::string& str) {// 防止SQL注入
    if (str.empty()) return "";
    std::vector<char> buf(str.size() * 2 + 1);
    mysql_real_escape_string(conn, buf.data(), str.c_str(), str.size());
    return std::string(buf.data());
}

uint64_t StorageManager::next_free_id(MYSQL* conn, const std::string& table,
                                      const std::string& id_col) {// 最小空缺 id（从 1 开始，不跳过）
    std::string q = "SELECT " + id_col + " FROM " + table + " ORDER BY " + id_col + " ASC";
    if (mysql_query(conn, q.c_str()) != 0) return 0;
    MYSQL_RES* res = mysql_store_result(conn);
    uint64_t next = 1;
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            uint64_t cur = row[0] ? std::stoull(row[0]) : 0;
            if (cur > next) break;  // 找到空缺
            if (cur == next) ++next;
        }
        mysql_free_result(res);
    }
    return next;
}

std::string StorageManager::generate_salt() {// 产生盐值
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    return bin_to_hex(buf, sizeof(buf));
}

std::string StorageManager::hash_password(const std::string& password, const std::string& salt) {// 对密码进行哈希处理
    unsigned char hash[SHA256_DIGEST_LENGTH];
    std::string combined = salt + password;
    SHA256(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);
    return bin_to_hex(hash, SHA256_DIGEST_LENGTH);
}

std::string StorageManager::generate_token() {// 产生token
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i) oss << std::setw(2) << static_cast<int>(buf[i]);
    return oss.str();
}

bool StorageManager::redis_reconnect() {// redis重新连接
    if (redis_ctx_) redisFree(redis_ctx_);
    redis_ctx_ = redisConnect(redis_host_.c_str(), redis_port_);
    return redis_ctx_ && !redis_ctx_->err;
}


// 宏封装，提高代码复用能力
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
        if (!ok) LOG(ERROR) << "[Redis] " << _r->str ;               \
        freeReplyObject(_r);                                                     \
    } while (0)

bool StorageManager::redis_set(const std::string& key, const std::string& value) {// 设置键值
    bool ok = false; REDIS_CMD(, "SET %s %s", key.c_str(), value.c_str()); return ok;
}
bool StorageManager::redis_set_ex(const std::string& key, const std::string& value, int sec) {// 带有过期时间
    bool ok = false; REDIS_CMD(, "SETEX %s %d %s", key.c_str(), sec, value.c_str()); return ok;
}
bool StorageManager::redis_set_nx_ex(const std::string& key, const std::string& value, int seconds) {// NX：用于频率限制
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return false;
    auto* r = reinterpret_cast<redisReply*>(
        redisCommand(redis_ctx_, "SET %s %s EX %d NX", key.c_str(), value.c_str(), seconds));
    if (!r) {
        if (redis_reconnect())
            r = reinterpret_cast<redisReply*>(
                redisCommand(redis_ctx_, "SET %s %s EX %d NX", key.c_str(), value.c_str(), seconds));
        if (!r) return false;
    }
    bool ok = (r->type == REDIS_REPLY_STATUS);  // NX 未命中时返回 NIL
    if (r->type == REDIS_REPLY_ERROR) LOG(ERROR) << "[Redis] " << r->str;
    freeReplyObject(r);
    return ok;
}
long long StorageManager::redis_incr(const std::string& key) {// 自增+1并返回新值
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return -1;
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "INCR %s", key.c_str()));
    if (!r) {
        if (redis_reconnect())
            r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "INCR %s", key.c_str()));
        if (!r) return -1;
    }
    long long v = (r->type == REDIS_REPLY_INTEGER) ? r->integer : -1;
    if (r->type == REDIS_REPLY_ERROR) LOG(ERROR) << "[Redis] " << r->str;
    freeReplyObject(r);
    return v;
}
bool StorageManager::redis_del(const std::string& key) {// 删除键
    bool ok = false; REDIS_CMD(, "DEL %s", key.c_str()); return ok;
}
bool StorageManager::redis_hset(const std::string& key, const std::string& f, const std::string& v) {// 设置哈希表字段
    bool ok = false; REDIS_CMD(, "HSET %s %s %s", key.c_str(), f.c_str(), v.c_str()); return ok;
}
bool StorageManager::redis_sadd(const std::string& key, const std::string& m) {// 向集合添加成员
    bool ok = false; REDIS_CMD(, "SADD %s %s", key.c_str(), m.c_str()); return ok;
}
bool StorageManager::redis_srem(const std::string& key, const std::string& m) {// 从集合移除成员
    bool ok = false; REDIS_CMD(, "SREM %s %s", key.c_str(), m.c_str()); return ok;
}
bool StorageManager::redis_lpush(const std::string& key, const std::string& v) {// 从列表头部插入元素
    bool ok = false; REDIS_CMD(, "LPUSH %s %s", key.c_str(), v.c_str()); return ok;
}
bool StorageManager::redis_expire(const std::string& key, int sec) {// 设置键过期时间
    bool ok = false; REDIS_CMD(, "EXPIRE %s %d", key.c_str(), sec); return ok;
}
bool StorageManager::redis_del_key(const std::string& key) { return redis_del(key); } // 删除键（兼容旧接口）

std::string StorageManager::redis_get(const std::string& key) {// 获取对应键值
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
std::string StorageManager::redis_hget(const std::string& key, const std::string& field) {// 获取哈希表的值
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return "";
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "HGET %s %s", key.c_str(), field.c_str()));
    if (!r || r->type == REDIS_REPLY_NIL || r->type == REDIS_REPLY_ERROR) {
        if (r) freeReplyObject(r);
        return "";
    }
    std::string res(r->str, r->len); freeReplyObject(r); return res;
}
std::vector<std::string> StorageManager::redis_smembers(const std::string& key) {// 获取集合全部成员
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return result;
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "SMEMBERS %s", key.c_str()));
    if (!r || r->type != REDIS_REPLY_ARRAY) { if (r) freeReplyObject(r); return result; }
    for (size_t i = 0; i < r->elements; ++i)
        result.emplace_back(r->element[i]->str, r->element[i]->len);
    freeReplyObject(r);
    return result;
}

bool StorageManager::redis_sismember(const std::string& key, const std::string& member) {// 判断成员是否在集合中
    std::lock_guard<std::mutex> lock(redis_mutex_);
    if (!redis_ctx_) return false;
    auto* r = reinterpret_cast<redisReply*>(redisCommand(redis_ctx_, "SISMEMBER %s %s", key.c_str(), member.c_str()));
    if (!r || r->type != REDIS_REPLY_INTEGER) { if (r) freeReplyObject(r); return false; }
    bool ok = (r->integer == 1); freeReplyObject(r); return ok;
}
std::vector<std::string> StorageManager::redis_lrange(const std::string& key, int start, int stop) {// 获取列表区间元素
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

bool StorageManager::user_exists(const std::string& username) {// 判断用户名是否已存在
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
                                          const std::string& nickname,
                                          const std::string& email,
                                          const std::string& phone) {// 创建新用户（含邮箱/手机号）
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
    std::string email_sql = email.empty() ? "NULL" : ("'" + escape_string(conn, email) + "'");
    std::string phone_sql = phone.empty() ? "NULL" : ("'" + escape_string(conn, phone) + "'");

    // 始终新建用户，不复用已注销账号的 ID，避免残留旧数据随 ID 复活
    std::string q;
    if (mysql_query(conn, "LOCK TABLES users WRITE") != 0) {
        mysql_pool_->release(conn);
        result.success = false; result.error_message = "数据库锁表失败"; return result;
    }
    uint64_t free_id = next_free_id(conn, "users");
    if (free_id == 0) {
        mysql_query(conn, "UNLOCK TABLES");
        mysql_pool_->release(conn);
        result.success = false; result.error_message = "分配用户ID失败"; return result;
    }
    q = "INSERT INTO users (id, username, password_hash, salt, nickname, email, phone) VALUES ("
                   + std::to_string(free_id) + ", '" + eu + "', '" + hash + "', '" + salt
                   + "', '" + en + "', " + email_sql + ", " + phone_sql + ")";
    int rc = mysql_query(conn, q.c_str());
    mysql_query(conn, "UNLOCK TABLES");
    if (rc != 0) {
        result.success = false;
        result.error_message = std::string("创建用户失败: ") + mysql_error(conn);
        mysql_pool_->release(conn); return result;
    }
    uint64_t user_id = free_id;
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

QueryResult StorageManager::get_user_by_username(const std::string& username) {// 按用户名查询用户
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

QueryResult StorageManager::get_user_by_id(uint64_t user_id) {// 按用户ID查询用户
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

QueryResult StorageManager::get_user_by_email(const std::string& email) {// 按邮箱查询用户
    QueryResult result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) { result.success = false; result.error_message = "数据库连接失败"; return result; }
    std::string e = escape_string(conn, email);
    std::string q = "SELECT id, username, nickname, status FROM users WHERE email='" + e + "'";
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

QueryResult StorageManager::get_user_by_phone(const std::string& phone) {// 按手机号查询用户
    QueryResult result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) { result.success = false; result.error_message = "数据库连接失败"; return result; }
    std::string e = escape_string(conn, phone);
    std::string q = "SELECT id, username, nickname, status FROM users WHERE phone='" + e + "'";
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

bool StorageManager::email_exists(const std::string& email) {// 判断邮箱是否已被绑定
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string e = escape_string(conn, email);
    std::string q = "SELECT COUNT(*) FROM users WHERE email='" + e + "'";
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

bool StorageManager::phone_exists(const std::string& phone) {// 判断手机号是否已被绑定
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string e = escape_string(conn, phone);
    std::string q = "SELECT COUNT(*) FROM users WHERE phone='" + e + "'";
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

bool StorageManager::verify_password(const std::string& username, const std::string& password) {// 校验用户名密码是否匹配
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

bool StorageManager::update_password(uint64_t user_id, const std::string& new_password) {// 更新密码（重新加盐哈希）
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

bool StorageManager::update_nickname(uint64_t user_id, const std::string& nickname) {// 更新昵称
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string en = escape_string(conn, nickname);
    std::string q = "UPDATE users SET nickname='" + en + "' WHERE id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    if (ok) redis_hset("chatroom:session:" + std::to_string(user_id), "nickname", nickname);
    return ok;
}

std::string StorageManager::create_session(uint64_t user_id, const std::string& username) {// 创建登录会话并返回token
    std::string token = generate_token();
    std::string sk = "chatroom:session:" + std::to_string(user_id);
    redis_hset(sk, "username", username);
    redis_hset(sk, "token", token);
    redis_hset(sk, "login_time", now_str());
    redis_expire(sk, 86400);
    redis_set_ex("chatroom:token:" + token, std::to_string(user_id), 86400);
    return token;
}

bool StorageManager::verify_token(const std::string& token) {// 校验token是否有效
    return !redis_get("chatroom:token:" + token).empty();
}

bool StorageManager::save_verify_code(const std::string& scene, const std::string& channel,
                                      const std::string& target, const std::string& code, int ttl_sec) {
    return redis_set_ex(vcode_key(scene, channel, target), code, ttl_sec);// 保存验证码
}

std::string StorageManager::load_verify_code(const std::string& scene, const std::string& channel,
                                             const std::string& target) {// 获取验证码
    return redis_get(vcode_key(scene, channel, target));
}

void StorageManager::del_verify_code(const std::string& scene, const std::string& channel,
                                     const std::string& target) {// 删除验证码
    redis_del(vcode_key(scene, channel, target));
    redis_del(vcode_try_key(scene, channel, target));
}

bool StorageManager::try_verify_rate_limit(const std::string& scene, const std::string& channel,
                                           const std::string& target, int cooldown_sec) {// 验证码发送频率限制
    // 返回 true 表示允许发送（key 不存在、首次）；false 表示冷却中
    return redis_set_nx_ex(vcode_rate_key(scene, channel, target), "1", cooldown_sec);
}

int64_t StorageManager::incr_verify_attempt(const std::string& scene, const std::string& channel,
                                            const std::string& target, int ttl_sec) {// 累加验证码错误尝试次数
    std::string key = vcode_try_key(scene, channel, target);
    int64_t v = redis_incr(key);
    if (v == 1) redis_expire(key, ttl_sec);  // 首次自增时设置 TTL
    return v;
}

bool StorageManager::delete_user(uint64_t user_id) {// 注销用户
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string uid = std::to_string(user_id);

    std::vector<uint64_t> transfer_ids;
    {
        std::string sel = "SELECT transfer_id FROM file_transfers WHERE sender_id="
                         + uid + " OR receiver_id=" + uid;
        if (mysql_query(conn, sel.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row;
            while (res && (row = mysql_fetch_row(res)))
                transfer_ids.push_back(std::stoull(row[0]));
            if (res) mysql_free_result(res);
        }
    }

    // 收集磁盘文件路径（删行前）
    std::vector<std::string> file_paths;
    {
        std::string sel = "SELECT file_path FROM files WHERE uploader_id="
                         + uid + " OR target_id=" + uid;
        if (mysql_query(conn, sel.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row;
            while (res && (row = mysql_fetch_row(res)))
                file_paths.push_back(row[0] ? row[0] : "");
            if (res) mysql_free_result(res);
        }
    }

    // 收集用户所属群（清缓存用，删成员行前）
    std::vector<uint64_t> joined_groups;
    {
        std::string sel = "SELECT group_id FROM group_members WHERE user_id=" + uid;
        if (mysql_query(conn, sel.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row;
            while (res && (row = mysql_fetch_row(res)))
                joined_groups.push_back(std::stoull(row[0]));
            if (res) mysql_free_result(res);
        }
    }

    // 收集所有群（清除该用户在各群待审批列表中的残留）
    std::vector<uint64_t> all_groups;
    {
        if (mysql_query(conn, "SELECT id FROM chat_groups") == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row;
            while (res && (row = mysql_fetch_row(res)))
                all_groups.push_back(std::stoull(row[0]));
            if (res) mysql_free_result(res);
        }
    }

    std::string del_msg = "DELETE FROM messages WHERE sender_id=" + uid
                         + " OR target_id=" + uid;
    mysql_query(conn, del_msg.c_str());
    std::string del_friends = "DELETE FROM friendships WHERE user_id=" + uid
                             + " OR friend_id=" + uid;
    mysql_query(conn, del_friends.c_str());
    std::string del_gm = "DELETE FROM group_members WHERE user_id=" + uid;
    mysql_query(conn, del_gm.c_str());

    // 解散该用户创建的群组：清理群消息、成员关系、群组本身与缓存
    std::vector<uint64_t> owned_groups;
    {
        std::string sel = "SELECT id FROM chat_groups WHERE owner_id=" + uid;
        if (mysql_query(conn, sel.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row;
            while (res && (row = mysql_fetch_row(res)))
                owned_groups.push_back(std::stoull(row[0]));
            if (res) mysql_free_result(res);
        }
    }
    for (uint64_t gid : owned_groups) {
        std::string gid_s = std::to_string(gid);
        std::string del_gmsg = "DELETE FROM messages WHERE group_id=" + gid_s;
        mysql_query(conn, del_gmsg.c_str());
        std::string del_gmemb = "DELETE FROM group_members WHERE group_id=" + gid_s;
        mysql_query(conn, del_gmemb.c_str());
        std::string del_g = "DELETE FROM chat_groups WHERE id=" + gid_s;
        mysql_query(conn, del_g.c_str());
        redis_del("chatroom:group:" + gid_s + ":members");
        redis_del("chatroom:group:" + gid_s + ":join_requests");
    }
    std::string del_files = "DELETE FROM files WHERE uploader_id=" + uid
                           + " OR target_id=" + uid;
    mysql_query(conn, del_files.c_str());
    std::string del_xfer = "DELETE FROM file_transfers WHERE sender_id=" + uid
                          + " OR receiver_id=" + uid;
    mysql_query(conn, del_xfer.c_str());

    std::string eu = escape_string(conn, "_del_" + uid);
    std::string q = "UPDATE users SET status=3, username='" + eu
                   + "', password_hash='', salt='', nickname='' WHERE id=" + uid;
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);

    // 清理 Redis
    std::string token = redis_hget("chatroom:session:" + uid, "token");
    if (!token.empty()) redis_del("chatroom:token:" + token);
    redis_del("chatroom:session:" + uid);
    redis_srem("chatroom:online", uid);
    redis_del("chatroom:user:" + uid + ":friends");
    redis_del("chatroom:user:" + uid + ":groups");
    redis_del("chatroom:offline_msg:" + uid);
    redis_del("chatroom:transfer:pending:" + uid);

    // 清理所属群缓存与该用户在群中的待审批残留
    for (uint64_t gid : joined_groups) {
        redis_del("chatroom:group:" + std::to_string(gid) + ":members");
    }
    for (uint64_t gid : all_groups) {
        redis_srem("chatroom:group:" + std::to_string(gid) + ":join_requests", uid);
    }

    // 清理磁盘上的已接收/已发送文件
    for (const auto &path : file_paths) {
        if (!path.empty()) remove(path.c_str());
    }

    // 清理文件传输的分片数据与 Redis 键
    for (uint64_t tid : transfer_ids) {
        redis_del("chatroom:transfer:" + std::to_string(tid) + ":sender_chunks");
        redis_del("chatroom:transfer:" + std::to_string(tid) + ":receiver_chunks");
        std::string chunk_dir = std::string(FILE_STORAGE_BASE) + "/transfer_" + std::to_string(tid);
        std::string rm_cmd = "rm -rf " + chunk_dir;
        system(rm_cmd.c_str());
    }

    return ok;
}

bool StorageManager::clear_session(uint64_t user_id) {// 清除用户会话
    std::string sk = "chatroom:session:" + std::to_string(user_id);
    std::string token = redis_hget(sk, "token");
    if (!token.empty()) redis_del("chatroom:token:" + token);
    redis_del(sk);
    return true;
}

bool StorageManager::add_friend(uint64_t user_id, uint64_t friend_id) {// 添加好友
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    if (mysql_query(conn, "LOCK TABLES friendships WRITE") != 0) {
        mysql_pool_->release(conn); return false;
    }
    uint64_t id1 = next_free_id(conn, "friendships");
    uint64_t id2 = (id1 == 0) ? 0 : next_free_id(conn, "friendships", "id");
    if (id1 == 0 || id2 == 0) {
        mysql_query(conn, "UNLOCK TABLES");
        mysql_pool_->release(conn); return false;
    }
    std::string q1 = "INSERT IGNORE INTO friendships (id, user_id, friend_id) VALUES ("
                    + std::to_string(id1) + ", " + std::to_string(user_id)
                    + ", " + std::to_string(friend_id) + ")";
    std::string q2 = "INSERT IGNORE INTO friendships (id, user_id, friend_id) VALUES ("
                    + std::to_string(id2) + ", " + std::to_string(friend_id)
                    + ", " + std::to_string(user_id) + ")";
    bool ok = (mysql_query(conn, q1.c_str()) == 0 && mysql_query(conn, q2.c_str()) == 0);
    mysql_query(conn, "UNLOCK TABLES");
    mysql_pool_->release(conn);
    redis_del("chatroom:user:" + std::to_string(user_id) + ":friends");// 保证缓存的一致性，因此在这里进行删除缓存
    redis_del("chatroom:user:" + std::to_string(friend_id) + ":friends");
    return ok;
}

bool StorageManager::remove_friend(uint64_t user_id, uint64_t friend_id) {// 删除好友
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q1 = "DELETE FROM friendships WHERE user_id=" + std::to_string(user_id)
                    + " AND friend_id=" + std::to_string(friend_id);
    std::string q2 = "DELETE FROM friendships WHERE user_id=" + std::to_string(friend_id)
                    + " AND friend_id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q1.c_str()) == 0 && mysql_query(conn, q2.c_str()) == 0);

    // 删除好友时同时清除双方私聊记录
    std::string uid = std::to_string(user_id), fid = std::to_string(friend_id);
    std::string del_msg = "DELETE FROM messages WHERE group_id IS NULL "
                          "AND ((sender_id=" + uid + " AND target_id=" + fid + ") "
                          "OR (sender_id=" + fid + " AND target_id=" + uid + "))";
    mysql_query(conn, del_msg.c_str());

    mysql_pool_->release(conn);
    redis_del("chatroom:user:" + std::to_string(user_id) + ":friends");
    redis_del("chatroom:user:" + std::to_string(friend_id) + ":friends");
    return ok;
}

bool StorageManager::is_friend(uint64_t user_id, uint64_t friend_id) {// 判断是否为好友
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

bool StorageManager::block_friend(uint64_t user_id, uint64_t friend_id) {// 拉黑好友
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE friendships SET is_blocked=TRUE WHERE user_id="
                   + std::to_string(user_id) + " AND friend_id=" + std::to_string(friend_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::unblock_friend(uint64_t user_id, uint64_t friend_id) {// 取消拉黑好友
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE friendships SET is_blocked=FALSE WHERE user_id="
                   + std::to_string(user_id) + " AND friend_id=" + std::to_string(friend_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::is_blocked_by(uint64_t user_id, uint64_t friend_id) {// 判断是否被对方拉黑
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

std::vector<QueryResult::FriendInfo> StorageManager::get_blocked_users(uint64_t user_id) {// 获取黑名单列表
    std::vector<QueryResult::FriendInfo> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    std::string q = "SELECT u.id, u.username, u.nickname, f.is_blocked, UNIX_TIMESTAMP(f.created_at) "
                    "FROM friendships f JOIN users u ON f.friend_id = u.id "
                    "WHERE f.user_id=" + std::to_string(user_id) + " AND f.is_blocked=TRUE";
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return result; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return result; }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        QueryResult::FriendInfo info;
        info.user_id   = std::stoull(row[0]);
        info.username  = row[1] ? row[1] : "";
        info.nickname  = row[2] ? row[2] : "";
        info.is_blocked = true;
        info.add_time  = row[4] ? std::stoull(row[4]) : 0;
        info.is_online = is_online(info.user_id);
        result.push_back(info);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

std::vector<QueryResult::FriendInfo> StorageManager::get_friends(uint64_t user_id) {// 获取好友列表
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
        info.streak_days = get_streak_days(user_id, info.user_id);
        result.push_back(info);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

uint64_t StorageManager::get_streak_days(uint64_t user_id, uint64_t peer_id) {// 计算两人连续聊天天数
    if (user_id == 0 || peer_id == 0) return 0;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return 0;
    std::string q = "SELECT DISTINCT DATE(created_at) FROM messages "
                    "WHERE group_id IS NULL "
                    "AND ((sender_id=" + std::to_string(user_id) + " AND target_id=" + std::to_string(peer_id) + ") "
                    "OR (sender_id=" + std::to_string(peer_id) + " AND target_id=" + std::to_string(user_id) + "))";
    if (mysql_query(conn, q.c_str()) != 0) { mysql_pool_->release(conn); return 0; }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) { mysql_pool_->release(conn); return 0; }

    auto to_day = [](const std::string& date) -> int64_t {
        struct tm tm = {};
        if (!strptime(date.c_str(), "%Y-%m-%d", &tm)) return -1;
        return static_cast<int64_t>(timegm(&tm)) / 86400;
    };

    std::vector<int64_t> days;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (!row[0]) continue;
        int64_t d = to_day(row[0]);
        if (d >= 0) days.push_back(d);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);

    if (days.empty()) return 0;
    std::sort(days.begin(), days.end());
    days.erase(std::unique(days.begin(), days.end()), days.end());

    // 从今天(或昨天，若今天还没聊)开始向前统计连续天数
    time_t now = time(nullptr);
    struct tm local_tm = *localtime(&now);
    local_tm.tm_hour = local_tm.tm_min = local_tm.tm_sec = 0;
    int64_t today = static_cast<int64_t>(timegm(&local_tm)) / 86400;
    if (days.back() < today) today = days.back();

    uint64_t streak = 0;
    for (auto it = days.rbegin(); it != days.rend(); ++it) {
        if (*it == today - static_cast<int64_t>(streak)) ++streak;
        else break;
    }
    return streak;
}

uint64_t StorageManager::create_group(const std::string& group_name,
                                        const std::string& description,
                                        uint64_t owner_id, bool is_public) {// 创建群组
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return 0;
    std::string eg = escape_string(conn, group_name);
    std::string ed = escape_string(conn, description);

    uint64_t group_id = 0;
    std::string find_q = "SELECT id FROM chat_groups WHERE member_count=0 LIMIT 1";
    if (mysql_query(conn, find_q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            group_id = std::stoull(row[0]);
            std::string up_q = "UPDATE chat_groups SET group_name='" + eg
                             + "', description='" + ed
                             + "', owner_id=" + std::to_string(owner_id)
                             + ", is_public=" + (is_public ? "TRUE" : "FALSE")
                             + ", member_count=1 WHERE id=" + std::to_string(group_id);
            if (mysql_query(conn, up_q.c_str()) != 0) group_id = 0;
        }
        mysql_free_result(res);
    }

    if (group_id == 0) {
        if (mysql_query(conn, "LOCK TABLES chat_groups WRITE") != 0) {
            mysql_pool_->release(conn); return 0;
        }
        uint64_t free_id = next_free_id(conn, "chat_groups");
        std::string q = "INSERT INTO chat_groups (id, group_name, description, owner_id, is_public) VALUES ("
                       + std::to_string(free_id) + ", '" + eg + "', '" + ed + "', "
                       + std::to_string(owner_id) + ", " + (is_public ? "TRUE" : "FALSE") + ")";
        int rc = mysql_query(conn, q.c_str());
        mysql_query(conn, "UNLOCK TABLES");
        if (rc != 0 || free_id == 0) { mysql_pool_->release(conn); return 0; }
        group_id = free_id;
    }

    std::string del_old = "DELETE FROM group_members WHERE group_id=" + std::to_string(group_id);
    mysql_query(conn, del_old.c_str());
    std::string mq;
    if (mysql_query(conn, "LOCK TABLES group_members WRITE") != 0) {
        mysql_pool_->release(conn); return 0;
    }
    uint64_t mid = next_free_id(conn, "group_members");
    if (mid == 0) {
        mysql_query(conn, "UNLOCK TABLES");
        mysql_pool_->release(conn); return 0;
    }
    mq = "INSERT INTO group_members (id, group_id, user_id, role) VALUES ("
        + std::to_string(mid) + ", " + std::to_string(group_id) + ", "
        + std::to_string(owner_id) + ", 'owner')";
    int mrc = mysql_query(conn, mq.c_str());
    mysql_query(conn, "UNLOCK TABLES");
    mysql_pool_->release(conn);
    if (mrc != 0) return 0;
    redis_del("chatroom:user:" + std::to_string(owner_id) + ":groups");
    return group_id;
}

bool StorageManager::dismiss_group(uint64_t group_id, uint64_t requester_id) {// 解散群组（仅群主）
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
    std::string del_members = "DELETE FROM group_members WHERE group_id=" + std::to_string(group_id);
    mysql_query(conn, del_members.c_str());
    std::string del_msgs = "DELETE FROM messages WHERE group_id=" + std::to_string(group_id);
    mysql_query(conn, del_msgs.c_str());
    std::string q = "UPDATE chat_groups SET member_count=0 WHERE id=" + std::to_string(group_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    return ok;
}

bool StorageManager::join_group(uint64_t group_id, uint64_t user_id) {// 加入群组
    if (is_group_member(group_id, user_id)) return false;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    if (mysql_query(conn, "LOCK TABLES group_members WRITE") != 0) {
        mysql_pool_->release(conn); return false;
    }
    uint64_t mid = next_free_id(conn, "group_members");
    std::string q = "INSERT INTO group_members (id, group_id, user_id, role) VALUES ("
                   + std::to_string(mid) + ", " + std::to_string(group_id) + ", "
                   + std::to_string(user_id) + ", 'member')";
    bool ok = (mid != 0 && mysql_query(conn, q.c_str()) == 0);
    mysql_query(conn, "UNLOCK TABLES");
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

bool StorageManager::quit_group(uint64_t group_id, uint64_t user_id) {// 退出群组（群主退出自动转让）
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;

    // 检查是否为群主，如果是则寻找继承者
    std::string own_q = "SELECT owner_id FROM chat_groups WHERE id=" + std::to_string(group_id);
    bool is_owner = false;
    if (mysql_query(conn, own_q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        is_owner = (row && std::stoull(row[0]) == user_id);
        mysql_free_result(res);
    }

    // 如果是群主，在退出前转让群主身份
    if (is_owner) {
        // 优先寻找管理员继承，其次按加入时间最早的成员
        std::string find_q =
            "SELECT user_id FROM group_members "
            "WHERE group_id=" + std::to_string(group_id) + " AND user_id!=" + std::to_string(user_id) +
            " ORDER BY CASE WHEN role='admin' THEN 0 ELSE 1 END, joined_at ASC LIMIT 1";
        uint64_t successor = 0;
        if (mysql_query(conn, find_q.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) successor = std::stoull(row[0]);
            mysql_free_result(res);
        }
        if (successor != 0) {
            // 转让群主
            std::string transf = "UPDATE chat_groups SET owner_id=" + std::to_string(successor)
                               + " WHERE id=" + std::to_string(group_id);
            mysql_query(conn, transf.c_str());
            std::string up_role = "UPDATE group_members SET role='owner' WHERE group_id="
                                + std::to_string(group_id) + " AND user_id=" + std::to_string(successor);
            mysql_query(conn, up_role.c_str());
        }
    }

    std::string q = "DELETE FROM group_members WHERE group_id=" + std::to_string(group_id)
                   + " AND user_id=" + std::to_string(user_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    if (ok) {
        std::string up = "UPDATE chat_groups SET member_count = GREATEST(member_count - 1, 0) WHERE id="
                        + std::to_string(group_id);
        mysql_query(conn, up.c_str());
    }
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    redis_del("chatroom:user:" + std::to_string(user_id) + ":groups");
    return ok;
}

bool StorageManager::add_admin(uint64_t group_id, uint64_t owner_id, uint64_t user_id) {// 设置管理员（仅群主）
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

bool StorageManager::remove_admin(uint64_t group_id, uint64_t owner_id, uint64_t user_id) {// 取消管理员（仅群主）
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

bool StorageManager::approve_join(uint64_t group_id, uint64_t admin_id, uint64_t user_id) {// 审批通过入群申请
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
    if (!can) return false;

    // 检查是否在待审批列表中
    if (!is_join_pending(group_id, user_id)) return false;

    // 从待审批列表中移除
    std::string key = "chatroom:group:" + std::to_string(group_id) + ":join_requests";
    redis_srem(key, std::to_string(user_id));

    return join_group(group_id, user_id);
}

bool StorageManager::reject_join(uint64_t group_id, uint64_t admin_id, uint64_t user_id) {// 拒绝入群申请
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
    if (!can) return false;

    // 从待审批列表中移除
    std::string key = "chatroom:group:" + std::to_string(group_id) + ":join_requests";
    return redis_srem(key, std::to_string(user_id));
}

bool StorageManager::transfer_group_ownership(uint64_t group_id, uint64_t new_owner_id) {// 转让群主身份
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string up1 = "UPDATE chat_groups SET owner_id=" + std::to_string(new_owner_id)
                    + " WHERE id=" + std::to_string(group_id);
    std::string up2 = "UPDATE group_members SET role='owner' WHERE group_id="
                    + std::to_string(group_id) + " AND user_id=" + std::to_string(new_owner_id);
    bool ok = (mysql_query(conn, up1.c_str()) == 0 && mysql_query(conn, up2.c_str()) == 0);
    mysql_pool_->release(conn);
    redis_del("chatroom:group:" + std::to_string(group_id) + ":members");
    return ok;
}

std::vector<uint64_t> StorageManager::get_pending_join_requests(uint64_t group_id) {// 获取待审批入群申请
    std::vector<uint64_t> result;
    std::string key = "chatroom:group:" + std::to_string(group_id) + ":join_requests";
    auto members = redis_smembers(key);
    for (const auto& m : members) {
        try {
            result.push_back(std::stoull(m));
        } catch (...) {}
    }
    return result;
}

bool StorageManager::remove_member(uint64_t group_id, uint64_t admin_id, uint64_t user_id) {// 移除群成员
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

bool StorageManager::is_group_member(uint64_t group_id, uint64_t user_id) {// 判断是否为群成员
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

bool StorageManager::is_group_public(uint64_t group_id) {// 判断群组是否公开
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return true;
    std::string q = "SELECT is_public FROM chat_groups WHERE id=" + std::to_string(group_id);
    bool result = true;
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        result = (row && std::stoi(row[0]) != 0);
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return result;
}

bool StorageManager::request_join_group(uint64_t group_id, uint64_t user_id) {// 发起入群申请
    if (is_group_member(group_id, user_id)) return false;
    std::string key = "chatroom:group:" + std::to_string(group_id) + ":join_requests";
    return redis_sadd(key, std::to_string(user_id));
}

bool StorageManager::is_join_pending(uint64_t group_id, uint64_t user_id) {// 判断是否在待审批列表
    std::string key = "chatroom:group:" + std::to_string(group_id) + ":join_requests";
    return redis_sismember(key, std::to_string(user_id));
}

QueryResult::GroupInfo StorageManager::get_group_info(uint64_t group_id) {// 获取群组信息
    QueryResult::GroupInfo info{};
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return info;
    std::string q = "SELECT id, group_name, description, owner_id, member_count, is_public "
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
            info.is_public   = row[5] ? (std::stoi(row[5]) != 0) : true;
        }
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return info;
}

std::vector<QueryResult::GroupInfo> StorageManager::get_user_groups(uint64_t user_id) {// 获取用户加入的群组列表
    std::vector<QueryResult::GroupInfo> result;
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return result;
    std::string q = "SELECT g.id, g.group_name, g.description, g.owner_id, g.member_count, g.is_public, gm.role "
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
        info.is_public   = row[5] ? (std::stoi(row[5]) != 0) : true;
        info.role        = row[6] ? row[6] : "member";
        result.push_back(info);
    }
    mysql_free_result(res);
    mysql_pool_->release(conn);
    return result;
}

std::vector<QueryResult::GroupMember> StorageManager::get_group_members(uint64_t group_id) {// 获取群成员列表（含待审批）
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

    auto pending_ids = get_pending_join_requests(group_id);
    for (uint64_t pid : pending_ids) {
        auto pinfo = get_user_by_id(pid);
        if (pinfo.success) {
            QueryResult::GroupMember member;
            member.user_id   = pid;
            member.username  = pinfo.username;
            member.nickname  = pinfo.nickname;
            member.role      = "pending";
            member.join_time = 0;
            result.push_back(member);
        }
    }

    return result;
}

bool StorageManager::save_message(uint64_t sender_id, uint64_t target_id,
                                    const std::string& content, int message_type) {// 保存私聊消息
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ec = escape_string(conn, content);
    if (mysql_query(conn, "LOCK TABLES messages WRITE") != 0) {
        mysql_pool_->release(conn); return false;
    }
    uint64_t mid = next_free_id(conn, "messages");
    std::string q = "INSERT INTO messages (id, sender_id, target_id, message_type, body) VALUES ("
                   + std::to_string(mid) + ", " + std::to_string(sender_id) + ", "
                   + std::to_string(target_id) + ", "
                   + std::to_string(message_type) + ", '" + ec + "')";
    bool ok = (mid != 0 && mysql_query(conn, q.c_str()) == 0);
    mysql_query(conn, "UNLOCK TABLES");
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::save_group_message(uint64_t group_id, uint64_t sender_id,
                                          const std::string& content) {// 保存群聊消息
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string ec = escape_string(conn, content);
    if (mysql_query(conn, "LOCK TABLES messages WRITE") != 0) {
        mysql_pool_->release(conn); return false;
    }
    uint64_t mid = next_free_id(conn, "messages");
    std::string q = "INSERT INTO messages (id, sender_id, group_id, message_type, body) VALUES ("
                   + std::to_string(mid) + ", " + std::to_string(sender_id) + ", "
                   + std::to_string(group_id)
                   + ", 302, '" + ec + "')";
    bool ok = (mid != 0 && mysql_query(conn, q.c_str()) == 0);
    mysql_query(conn, "UNLOCK TABLES");
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::save_offline_message(uint64_t user_id, uint64_t sender_id,
                                            const std::string& sender_name,
                                            const std::string& content) {// 保存离线消息
    std::string key = "chatroom:offline_msg:" + std::to_string(user_id);
    std::string msg = std::to_string(sender_id) + "|" + sender_name + "|"
                     + content + "|" + now_str();
    bool ok = redis_lpush(key, msg);
    if (ok) redis_expire(key, 604800);
    return ok;
}

std::vector<QueryResult::MessageHistory> StorageManager::get_history(
    uint64_t user_id, uint64_t peer_id, int limit) {// 获取私聊历史记录
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

std::vector<QueryResult::MessageHistory> StorageManager::get_group_history(uint64_t group_id, int limit) {// 获取群聊历史记录
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

std::vector<QueryResult::MessageHistory> StorageManager::get_offline_messages(uint64_t user_id) {// 获取并清空离线消息
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

bool StorageManager::mark_read(uint64_t message_id) {// 标记消息已读
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE messages SET is_read=TRUE WHERE id=" + std::to_string(message_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

uint64_t StorageManager::create_transfer(uint64_t sender_id, uint64_t receiver_id,
                                         const std::string& file_name, uint64_t file_size,
                                         uint32_t total_chunks, const std::string& file_hash) {// 创建文件传输记录
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return 0;
    std::string ef = escape_string(conn, file_name);
    std::string efh = escape_string(conn, file_hash);
    if (mysql_query(conn, "LOCK TABLES file_transfers WRITE") != 0) {
        mysql_pool_->release(conn); return 0;
    }
    uint64_t free_id = next_free_id(conn, "file_transfers", "transfer_id");
    std::string q = "INSERT INTO file_transfers (transfer_id, sender_id, receiver_id, file_name, file_size, total_chunks, file_hash) VALUES ("
                   + std::to_string(free_id) + ", " + std::to_string(sender_id) + ", "
                   + std::to_string(receiver_id) + ", '"
                   + ef + "', " + std::to_string(file_size) + ", "
                   + std::to_string(total_chunks) + ", '" + efh + "')";
    int rc = mysql_query(conn, q.c_str());
    mysql_query(conn, "UNLOCK TABLES");
    if (rc != 0 || free_id == 0) {
        LOG(ERROR) << "[StorageManager] create_transfer INSERT failed: "
                  << mysql_error(conn) ;
        mysql_pool_->release(conn); return 0;
    }
    uint64_t tid = free_id;
    mysql_pool_->release(conn);
    mkdir(FILE_STORAGE_BASE, 0755);
    std::string dir = std::string(FILE_STORAGE_BASE) + "/transfer_" + std::to_string(tid);
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        LOG(ERROR) << "[StorageManager] Failed to create directory: " << dir
                  << " (errno=" << errno << ")" ;
        return 0;
    }

    return tid;
}

StorageManager::TransferInfo StorageManager::get_transfer_info(uint64_t transfer_id) {// 获取文件传输信息
    TransferInfo info{};
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return info;
    std::string q = "SELECT transfer_id, sender_id, receiver_id, file_name, file_size, "
                    "total_chunks, file_hash, status, UNIX_TIMESTAMP(created_at) "
                    "FROM file_transfers WHERE transfer_id=" + std::to_string(transfer_id);
    if (mysql_query(conn, q.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            info.transfer_id = std::stoull(row[0]);
            info.sender_id   = std::stoull(row[1]);
            info.receiver_id = std::stoull(row[2]);
            info.file_name   = row[3] ? row[3] : "";
            info.file_size   = std::stoull(row[4]);
            info.total_chunks = static_cast<uint32_t>(std::stoul(row[5]));
            info.file_hash   = row[6] ? row[6] : "";
            info.status      = row[7] ? row[7] : "sending";
            info.created_at  = row[8] ? std::stoull(row[8]) : 0;
        }
        mysql_free_result(res);
    }
    mysql_pool_->release(conn);
    return info;
}

bool StorageManager::record_sender_chunk(uint64_t transfer_id, uint32_t chunk_seq) {// 记录发送方已上传分片
    std::string key = xfer_sender_key(transfer_id);
    bool ok = redis_sadd(key, std::to_string(chunk_seq));
    if (ok) redis_expire(key, 86400);  // 24h
    return ok;
}

bool StorageManager::record_receiver_chunk(uint64_t transfer_id, uint32_t chunk_seq) {// 记录接收方已下载分片
    std::string key = xfer_receiver_key(transfer_id);
    bool ok = redis_sadd(key, std::to_string(chunk_seq));
    if (ok) redis_expire(key, 86400);
    return ok;
}

std::vector<uint32_t> StorageManager::get_sender_chunks(uint64_t transfer_id) {// 获取发送方已上传分片序号
    std::vector<uint32_t> result;
    auto members = redis_smembers(xfer_sender_key(transfer_id));
    for (const auto& m : members) {
        try { result.push_back(static_cast<uint32_t>(std::stoul(m))); }
        catch (...) {}
    }
    return result;
}

std::vector<uint32_t> StorageManager::get_receiver_chunks(uint64_t transfer_id) {// 获取接收方已下载分片序号
    std::vector<uint32_t> result;
    auto members = redis_smembers(xfer_receiver_key(transfer_id));
    for (const auto& m : members) {
        try { result.push_back(static_cast<uint32_t>(std::stoul(m))); }
        catch (...) {}
    }
    return result;
}

bool StorageManager::save_transfer_chunk_data(uint64_t transfer_id, uint32_t chunk_seq,
                                              const std::string& data, const std::string& chunk_hash) {// 保存分片数据到文件
    std::string path = xfer_chunk_path(transfer_id, chunk_seq);
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;
    fwrite(data.data(), 1, data.size(), fp);
    fclose(fp);
    // 存储 chunk_hash 到 Redis
    if (!chunk_hash.empty()) {
        std::string hkey = "chatroom:transfer:" + std::to_string(transfer_id)
                         + ":chunk_hash:" + std::to_string(chunk_seq);
        redis_set(hkey, chunk_hash);
    }
    return true;
}

std::string StorageManager::get_transfer_chunk_data(uint64_t transfer_id, uint32_t chunk_seq) {// 读取分片文件数据
    std::string path = xfer_chunk_path(transfer_id, chunk_seq);
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return "";
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::string data(sz, '\0');
    fread(&data[0], 1, sz, fp);
    fclose(fp);
    return data;
}

std::string StorageManager::get_transfer_chunk_hash(uint64_t transfer_id, uint32_t chunk_seq) {// 获取分片哈希
    std::string hkey = "chatroom:transfer:" + std::to_string(transfer_id)
                     + ":chunk_hash:" + std::to_string(chunk_seq);
    return redis_get(hkey);
}

std::vector<StorageManager::TransferInfo> StorageManager::get_pending_transfers(uint64_t user_id) {// 获取用户待接收的传输
    std::vector<TransferInfo> result;
    auto members = redis_smembers(xfer_pending_key(user_id));
    for (const auto& m : members) {
        try {
            uint64_t tid = std::stoull(m);
            auto info = get_transfer_info(tid);
            if (info.transfer_id != 0) result.push_back(info);
        } catch (...) {}
    }
    return result;
}

bool StorageManager::add_pending_transfer(uint64_t user_id, uint64_t transfer_id) {// 添加待接收传输
    return redis_sadd(xfer_pending_key(user_id), std::to_string(transfer_id));
}

bool StorageManager::clear_pending_transfers(uint64_t user_id) {// 清空待接收传输
    return redis_del(xfer_pending_key(user_id));
}

bool StorageManager::complete_transfer(uint64_t transfer_id) {// 标记传输完成
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE file_transfers SET status='completed' WHERE transfer_id="
                   + std::to_string(transfer_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

bool StorageManager::reject_transfer(uint64_t transfer_id) {// 标记传输拒绝
    MYSQL* conn = mysql_pool_->acquire();
    if (!conn) return false;
    std::string q = "UPDATE file_transfers SET status='rejected' WHERE transfer_id="
                   + std::to_string(transfer_id);
    bool ok = (mysql_query(conn, q.c_str()) == 0);
    mysql_pool_->release(conn);
    return ok;
}

std::string StorageManager::assemble_final_file(uint64_t transfer_id,
                                                 const std::string& file_name,
                                                 const std::string& file_hash,
                                                 const std::string& role,
                                                 std::string& error_msg) {// 合并分片生成最终文件
    auto info = get_transfer_info(transfer_id);
    if (info.transfer_id == 0) {
        error_msg = "transfer not found";
        return "";
    }

    std::vector<uint32_t> chunks;
    if (role == "sender")
        chunks = get_sender_chunks(transfer_id);
    else
        chunks = get_receiver_chunks(transfer_id);

    std::set<uint32_t> have(chunks.begin(), chunks.end());
    for (uint32_t i = 0; i < info.total_chunks; ++i) {
        if (!have.count(i)) {
            error_msg = "missing chunks";
            return "";
        }
    }
    std::string dir = std::string(FILE_STORAGE_BASE) + "/complete";
    mkdir(dir.c_str(), 0755);

    std::string final_path = dir + "/" + std::to_string(transfer_id) + "_" + file_name;
    std::string temp_path = final_path + ".tmp";

    FILE* out = fopen(temp_path.c_str(), "wb");
    if (!out) {
        error_msg = "cannot create temp file";
        return "";
    }

    std::string computed_hash;
    for (uint32_t seq = 0; seq < info.total_chunks; ++seq) {
        std::string chunk_data = get_transfer_chunk_data(transfer_id, seq);
        if (chunk_data.empty()) {
            fclose(out);
            remove(temp_path.c_str());
            error_msg = "chunk " + std::to_string(seq) + " unreadable";
            return "";
        }
        fwrite(chunk_data.data(), 1, chunk_data.size(), out);
    }
    fclose(out);

    if (!file_hash.empty()) {
        FILE* fp = fopen(temp_path.c_str(), "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            std::string buf(sz, '\0');
            fread(&buf[0], 1, sz, fp);
            fclose(fp);

            unsigned char digest[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char*>(buf.data()),
                   buf.size(), digest);
            computed_hash = bin_to_hex(digest, SHA256_DIGEST_LENGTH);

            if (computed_hash != file_hash) {
                remove(temp_path.c_str());
                error_msg = "hash mismatch";
                return "";
            }
        }
    }

    if (rename(temp_path.c_str(), final_path.c_str()) != 0) {
        remove(temp_path.c_str());
        error_msg = "rename failed";
        return "";
    }

    if (role == "receiver") {
        std::string chunk_dir = std::string(FILE_STORAGE_BASE) + "/transfer_" + std::to_string(transfer_id);
        std::string rm_cmd = "rm -rf " + chunk_dir;
        system(rm_cmd.c_str());

        redis_del(xfer_sender_key(transfer_id));
        redis_del(xfer_receiver_key(transfer_id));
    }

    return final_path;
}

void StorageManager::set_online(uint64_t user_id)   { redis_sadd("chatroom:online", std::to_string(user_id)); } // 标记用户上线
void StorageManager::set_offline(uint64_t user_id)  { redis_srem("chatroom:online", std::to_string(user_id)); } // 标记用户下线
bool StorageManager::is_online(uint64_t user_id)     { return redis_sismember("chatroom:online", std::to_string(user_id)); } // 判断用户是否在线

} // namespace chatroom
