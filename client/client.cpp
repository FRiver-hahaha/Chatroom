/**
 * Chatroom 命令行测试客户端
 *
 * 协议：4 字节大端长度前缀 + Protobuf ChatMessage
 * 服务端：localhost:8080
 *
 * 编译：需要链接 protobuf 和 pthread
 */

#include "chatroom.pb.h"
#include "service/MessageType.hpp"
#include "config/Config.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <limits>
#include <set>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <sqlite3.h>

using chatroom::MessageType; // 简化枚举使用

// SHA-256 工具函数
static std::string sha256_hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        oss << std::setw(2) << static_cast<int>(hash[i]);
    return oss.str();
}

// ============================================================
// 本地 SQLite 存储（聊天记录落盘）
// ============================================================
class LocalStore {
public:
    bool open(const std::string& username) {
        std::lock_guard<std::mutex> lock(mu_);
        close_locked();
        std::string db_path = "chatroom_" + username + ".db";
        if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
            std::cerr << "[本地存储] 打开数据库失败: "
                      << (db_ ? sqlite3_errmsg(db_) : "未知错误") << std::endl;
            if (db_) { sqlite3_close(db_); db_ = nullptr; }
            return false;
        }
        const char* sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " chat_type INTEGER NOT NULL,"
            " peer_id INTEGER NOT NULL,"
            " sender_id INTEGER NOT NULL,"
            " sender_name TEXT,"
            " content TEXT,"
            " timestamp INTEGER,"
            " is_self INTEGER)";
        char* err = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "[本地存储] 建表失败: " << (err ? err : "未知错误") << std::endl;
            sqlite3_free(err);
        }
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mu_);
        close_locked();
    }

    // chat_type: 0=私聊, 1=群聊；peer_id: 私聊为对方 user_id，群聊为 group_id
    void save_message(int chat_type, uint64_t peer_id, uint64_t sender_id,
                      const std::string& sender_name, const std::string& content,
                      uint64_t timestamp, bool is_self) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!db_) return;
        const char* sql = "INSERT INTO messages "
                          "(chat_type, peer_id, sender_id, sender_name, content, timestamp, is_self) "
                          "VALUES (?,?,?,?,?,?,?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int(stmt, 1, chat_type);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(peer_id));
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(sender_id));
        sqlite3_bind_text(stmt, 4, sender_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(timestamp));
        sqlite3_bind_int(stmt, 7, is_self ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    void print_history(int chat_type, uint64_t peer_id, int limit) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!db_) { std::cout << "  (本地数据库未打开)" << std::endl; return; }
        std::string sql = "SELECT sender_name, content, timestamp, is_self FROM messages "
                          "WHERE chat_type=" + std::to_string(chat_type) +
                          " AND peer_id=" + std::to_string(peer_id) +
                          " ORDER BY id DESC LIMIT " + std::to_string(limit);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;
        std::vector<std::string> lines;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            uint64_t ts = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
            int is_self = sqlite3_column_int(stmt, 3);
            time_t t = static_cast<time_t>(ts);
            char time_buf[32];
            strftime(time_buf, sizeof(time_buf), "%m-%d %H:%M", localtime(&t));
            std::string who = is_self ? "[我]" : ("[" + std::string(name ? name : "") + "]");
            lines.push_back("  [" + std::string(time_buf) + "] " + who + ": " +
                            (content ? content : ""));
        }
        sqlite3_finalize(stmt);
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            std::cout << *it << std::endl;
        if (lines.empty()) std::cout << "  (本地无记录)" << std::endl;
    }

private:
    void close_locked() {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
    }
    sqlite3* db_ = nullptr;
    std::mutex mu_;
};

// ============================================================
// ChatClient 类
// ============================================================
class ChatClient {
public:
    ChatClient() = default;
    ~ChatClient() { disconnect(); }

    // ===== 连接管理 =====
    bool connect(const std::string& host, int port) {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "[错误] 创建 socket 失败" << std::endl;
            return false;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "[错误] 无效的地址: " << host << std::endl;
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[错误] 连接失败: " << strerror(errno) << std::endl;
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }

        running_.store(true);
        recv_thread_ = std::thread(&ChatClient::recv_loop, this);

        std::cout << "[信息] 已连接到 " << host << ":" << port << std::endl;
        return true;
    }

    void disconnect() {
        running_.store(false);
        if (sockfd_ >= 0) {
            shutdown(sockfd_, SHUT_RDWR);
            close(sockfd_);
            sockfd_ = -1;
        }
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
    }

    bool is_connected() const { return sockfd_ >= 0 && running_.load(); }
    bool is_logged_in() const { return !token_.empty(); }
    uint64_t user_id() const { return user_id_; }
    const std::string& username() const { return username_; }
    const std::string& token() const { return token_; }

    // ===== 发送消息 =====
    bool send_message(const chatroom::ChatMessage& msg) {
        std::string payload;
        if (!msg.SerializeToString(&payload)) {
            std::cerr << "[错误] Protobuf 序列化失败" << std::endl;
            return false;
        }
        return send_raw(payload);
    }

    bool send_raw(const std::string& data) {
        uint32_t len = htonl(data.size());
        std::string packet(4, '\0');
        memcpy(&packet[0], &len, 4);
        packet.append(data);

        size_t sent = 0;
        while (sent < packet.size()) {
            ssize_t n = ::send(sockfd_, packet.data() + sent, packet.size() - sent, 0);
            if (n <= 0) {
                std::cerr << "[错误] 发送失败: " << strerror(errno) << std::endl;
                return false;
            }
            sent += n;
        }
        return true;
    }

    // ===== 接收响应（阻塞等待指定类型） =====
    chatroom::ChatMessage wait_response(chatroom::MessageType expected_type, int timeout_sec = 5) {
        std::unique_lock<std::mutex> lock(resp_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);

        while (running_.load()) {
            // 在已有响应中查找
            for (auto it = responses_.begin(); it != responses_.end(); ++it) {
                if (it->type() == static_cast<uint32_t>(expected_type)) {
                    auto resp = *it;
                    responses_.erase(it);
                    return resp;
                }
            }

            // 也检查文本通知
            for (auto it = notifications_.begin(); it != notifications_.end(); ++it) {
                std::cout << "\r[通知] " << *it << std::endl;
                std::cout << "> " << std::flush;
            }
            notifications_.clear();

            if (resp_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                break;
            }
        }

        chatroom::ChatMessage empty;
        empty.set_type(0);
        return empty;
    }

    // 打印所有待处理的通知
    void flush_notifications() {
        std::lock_guard<std::mutex> lock(resp_mutex_);
        for (auto& n : notifications_) {
            std::cout << "[通知] " << n << std::endl;
        }
        notifications_.clear();
    }

    // ===== 业务接口 =====

    bool login(const std::string& user, const std::string& pass) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::LOGIN_REQ));
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_login_req();
        body->set_username(user);
        body->set_password(pass);

        if (!send_message(msg)) return false;

        auto resp = wait_response(MessageType::LOGIN_RSP);
        if (resp.type() != static_cast<uint32_t>(MessageType::LOGIN_RSP) || !resp.has_login_rsp()) {
            std::cerr << "[错误] 未收到登录响应" << std::endl;
            return false;
        }

        auto& r = resp.login_rsp();
        if (r.success()) {
            user_id_ = r.user_id();
            username_ = r.username();
            token_ = r.token();
            local_store_.open(username_);
            std::cout << "[成功] 登录成功! user_id=" << user_id_
                      << " username=" << username_ << std::endl;
            flush_notifications();
            return true;
        } else {
            std::cerr << "[失败] " << r.error_message() << std::endl;
            return false;
        }
    }

    bool register_user(const std::string& user, const std::string& pass,
                       const std::string& nickname) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::REGISTER_REQ));
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_register_req();
        body->set_username(user);
        body->set_password(pass);
        body->set_nickname(nickname);

        if (!send_message(msg)) return false;

        auto resp = wait_response(MessageType::REGISTER_RSP);
        if (resp.type() != static_cast<uint32_t>(MessageType::REGISTER_RSP) || !resp.has_register_rsp()) {
            std::cerr << "[错误] 未收到注册响应" << std::endl;
            return false;
        }

        auto& r = resp.register_rsp();
        if (r.success()) {
            std::cout << "[成功] 注册成功! user_id=" << r.user_id()
                      << " username=" << r.username() << std::endl;
            flush_notifications();
            return true;
        } else {
            std::cerr << "[失败] " << r.error_message() << std::endl;
            return false;
        }
    }

    bool logout() {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::LOGOUT_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        msg.mutable_logout_req();

        if (!send_message(msg)) return false;

        auto resp = wait_response(MessageType::LOGOUT_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::LOGOUT_RSP) && resp.has_logout_rsp()) {
            if (resp.logout_rsp().success()) {
                std::cout << "[成功] 已登出" << std::endl;
            }
        }

        user_id_ = 0;
        username_.clear();
        token_.clear();
        local_store_.close();
        return true;
    }

    bool delete_account(const std::string& password) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::DELETE_ACCOUNT_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        msg.mutable_delete_account_req()->set_password(password);

        if (!send_message(msg)) return false;

        auto resp = wait_response(MessageType::DELETE_ACCOUNT_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::DELETE_ACCOUNT_RSP) && resp.has_delete_account_rsp()) {
            if (resp.delete_account_rsp().success()) {
                std::cout << "[成功] 账号已注销" << std::endl;
                user_id_ = 0;
                username_.clear();
                token_.clear();
                return true;
            }
            std::cerr << "[失败] " << resp.delete_account_rsp().error_message() << std::endl;
        }
        return false;
    }

    // --- 好友 ---

    bool add_friend(uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::ADD_FRIEND_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_add_friend_req()->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::ADD_FRIEND_RSP);
        return handle_friend_op_resp(resp, "添加好友");
    }

    bool delete_friend(uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::DELETE_FRIEND_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_delete_friend_req()->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::DELETE_FRIEND_RSP);
        return handle_friend_op_resp(resp, "删除好友");
    }

    bool block_friend(uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::BLOCK_FRIEND_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_block_friend_req()->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::BLOCK_FRIEND_RSP);
        return handle_friend_op_resp(resp, "拉黑好友");
    }

    bool unblock_friend(uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::UNBLOCK_FRIEND_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_unblock_friend_req()->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::UNBLOCK_FRIEND_RSP);
        return handle_friend_op_resp(resp, "解除拉黑");
    }

    void query_blocked() {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::QUERY_BLOCKED_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        msg.mutable_query_blocked_req();

        if (!send_message(msg)) return;
        auto resp = wait_response(MessageType::QUERY_BLOCKED_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::QUERY_BLOCKED_RSP) && resp.has_query_blocked_rsp()) {
            auto& r = resp.query_blocked_rsp();
            if (r.success()) {
                std::cout << "\n===== 已拉黑用户列表 =====" << std::endl;
                std::cout << std::left << std::setw(10) << "UID"
                          << std::setw(18) << "用户名"
                          << std::setw(18) << "昵称"
                          << std::setw(8) << "在线" << std::endl;
                std::cout << std::string(54, '-') << std::endl;
                int count = 0;
                for (int i = 0; i < r.friends_size(); ++i) {
                    auto& f = r.friends(i);
                    std::cout << std::left << std::setw(10) << f.user_id()
                              << std::setw(18) << f.username()
                              << std::setw(18) << f.nickname()
                              << std::setw(8) << (f.is_online() ? "是" : "否") << std::endl;
                    count++;
                }
                if (count == 0) {
                    std::cout << "  (无已拉黑用户)" << std::endl;
                }
            } else {
                std::cerr << "[失败] " << r.error_message() << std::endl;
            }
        }
    }

    void query_friends() {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::QUERY_FRIEND_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        msg.mutable_query_friend_req();

        if (!send_message(msg)) return;
        auto resp = wait_response(MessageType::QUERY_FRIEND_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::QUERY_FRIEND_RSP) && resp.has_query_friend_rsp()) {
            auto& r = resp.query_friend_rsp();
            if (r.success()) {
                std::cout << "\n===== 好友列表 =====" << std::endl;
                std::cout << std::left << std::setw(10) << "UID"
                          << std::setw(18) << "用户名"
                          << std::setw(18) << "昵称"
                          << std::setw(8) << "在线"
                          << std::setw(8) << "已拉黑" << std::endl;
                std::cout << std::string(62, '-') << std::endl;
                for (int i = 0; i < r.friends_size(); ++i) {
                    auto& f = r.friends(i);
                    std::cout << std::left << std::setw(10) << f.user_id()
                              << std::setw(18) << f.username()
                              << std::setw(18) << f.nickname()
                              << std::setw(8) << (f.is_online() ? "是" : "否")
                              << std::setw(8) << (f.is_blocked() ? "是" : "否") << std::endl;
                }
                friend_cache_.clear();
                for (int i = 0; i < r.friends_size(); ++i) {
                    friend_cache_.push_back(r.friends(i));
                }
            } else {
                std::cerr << "[失败] " << r.error_message() << std::endl;
            }
        }
    }

    // --- 群组 ---

    uint64_t create_group(const std::string& name, const std::string& desc, bool is_public) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::CREATE_GROUP_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_create_group_req();
        body->set_group_name(name);
        body->set_description(desc);
        body->set_is_public(is_public);

        if (!send_message(msg)) return 0;
        auto resp = wait_response(MessageType::CREATE_GROUP_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::CREATE_GROUP_RSP) && resp.has_create_group_rsp()) {
            auto& r = resp.create_group_rsp();
            if (r.success()) {
                std::cout << "[成功] 群组创建成功! group_id=" << r.group_id() << std::endl;
                return r.group_id();
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
        }
        return 0;
    }

    bool join_group(uint64_t gid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::JOIN_GROUP_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_join_group_req()->set_group_id(gid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::JOIN_GROUP_RSP);
        return handle_group_op_resp(resp, "加入群组");
    }

    bool quit_group(uint64_t gid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::QUIT_GROUP_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_quit_group_req()->set_group_id(gid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::QUIT_GROUP_RSP);
        return handle_group_op_resp(resp, "退出群组");
    }

    bool dismiss_group(uint64_t gid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::DISMISS_GROUP_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_dismiss_group_req()->set_group_id(gid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::DISMISS_GROUP_RSP);
        return handle_group_op_resp(resp, "解散群组");
    }

    void query_group_list() {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::QUERY_GROUP_LIST_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        msg.mutable_query_group_list_req();

        if (!send_message(msg)) return;
        auto resp = wait_response(MessageType::QUERY_GROUP_LIST_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::QUERY_GROUP_LIST_RSP) && resp.has_query_group_list_rsp()) {
            auto& r = resp.query_group_list_rsp();
            if (r.success()) {
                std::cout << "\n===== 群组列表 =====" << std::endl;
                group_cache_.clear();
                for (int i = 0; i < r.groups_size(); ++i) {
                    auto& g = r.groups(i);
                    group_cache_.push_back(g);
                    std::cout << "  [" << g.group_id() << "] " << g.group_name()
                              << " (" << g.member_count() << "人)"
                              << (g.is_public() ? " [公开]" : " [私密]")
                              << (g.is_member() ? " [已加入]" : "") << std::endl;
                }
                if (r.groups_size() == 0) {
                    std::cout << "  (无群组)" << std::endl;
                }
            } else {
                std::cerr << "[失败] " << r.error_message() << std::endl;
            }
        }
    }

    void query_group_members(uint64_t gid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::QUERY_GROUP_MEMBERS_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_query_group_members_req()->set_group_id(gid);

        if (!send_message(msg)) return;
        auto resp = wait_response(MessageType::QUERY_GROUP_MEMBERS_RSP);
        if (resp.type() == static_cast<uint32_t>(MessageType::QUERY_GROUP_MEMBERS_RSP) && resp.has_query_group_members_rsp()) {
            auto& r = resp.query_group_members_rsp();
            if (r.success()) {
                std::cout << "\n===== 群成员 (group_id=" << gid << ") =====" << std::endl;
                for (int i = 0; i < r.members_size(); ++i) {
                    auto& m = r.members(i);
                    std::cout << "  [" << m.user_id() << "] " << m.nickname()
                              << " (@" << m.username() << ") [" << m.role() << "]" << std::endl;
                }
                if (r.members_size() == 0) {
                    std::cout << "  (无成员)" << std::endl;
                }
            } else {
                std::cerr << "[失败] " << r.error_message() << std::endl;
            }
        }
    }

    bool add_group_admin(uint64_t gid, uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::ADD_GROUP_ADMIN_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_add_group_admin_req();
        body->set_group_id(gid);
        body->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::ADD_GROUP_ADMIN_RSP);
        return handle_group_op_resp(resp, "添加管理员");
    }

    bool remove_group_admin(uint64_t gid, uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::REMOVE_GROUP_ADMIN_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_remove_group_admin_req();
        body->set_group_id(gid);
        body->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::REMOVE_GROUP_ADMIN_RSP);
        return handle_group_op_resp(resp, "移除管理员");
    }

    bool approve_join_group(uint64_t gid, uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::APPROVE_JOIN_GROUP_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_approve_join_group_req();
        body->set_group_id(gid);
        body->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::APPROVE_JOIN_GROUP_RSP);
        return handle_group_op_resp(resp, "批准加入");
    }

    bool reject_join_group(uint64_t gid, uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::REJECT_JOIN_GROUP_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_reject_join_group_req();
        body->set_group_id(gid);
        body->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::REJECT_JOIN_GROUP_RSP);
        return handle_group_op_resp(resp, "拒绝加入");
    }

    bool remove_group_member(uint64_t gid, uint64_t target_uid) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::REMOVE_GROUP_MEMBER_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_remove_group_member_req();
        body->set_group_id(gid);
        body->set_target_user_id(target_uid);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::REMOVE_GROUP_MEMBER_RSP);
        return handle_group_op_resp(resp, "移除群组成员");
    }

    // --- 聊天 ---

    bool send_private_chat(uint64_t target_uid, const std::string& text) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::PRIVATE_CHAT_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(target_uid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_private_chat_req()->set_payload(text);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::PRIVATE_CHAT_RSP);
        bool ok = handle_chat_resp(resp, "发送私聊");
        if (ok) local_store_.save_message(0, target_uid, user_id_, username_, text, time(nullptr), true);
        return ok;
    }

    bool send_group_chat(uint64_t gid, const std::string& text) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::GROUP_CHAT_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_group_id(gid);
        msg.set_timestamp(time(nullptr));
        msg.mutable_group_chat_req()->set_payload(text);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::GROUP_CHAT_RSP);
        bool ok = handle_chat_resp(resp, "发送群聊");
        if (ok) local_store_.save_message(1, gid, user_id_, username_, text, time(nullptr), true);
        return ok;
    }

    void get_history(uint64_t target_uid, uint64_t gid, int limit = 50) {
        chatroom::ChatMessage msg;
        // 根据 target_uid 或 gid 决定使用哪种历史请求
        if (gid > 0) {
            msg.set_type(static_cast<uint32_t>(MessageType::GET_HISTORY_REQ));
        } else {
            msg.set_type(static_cast<uint32_t>(MessageType::GET_HISTORY_REQ));
        }
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(target_uid);
        msg.set_group_id(gid);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_get_history_req();
        body->set_target_user_id(target_uid);
        body->set_group_id(gid);
        body->set_limit(limit);

        if (!send_message(msg)) return;
        auto resp = wait_response(MessageType::GET_HISTORY_RSP, 10);
        if (resp.type() == static_cast<uint32_t>(MessageType::GET_HISTORY_RSP) && resp.has_get_history_rsp()) {
            auto& r = resp.get_history_rsp();
            if (r.success()) {
                std::cout << "\n===== 聊天记录 (" << r.messages_size() << "条) =====" << std::endl;
                for (int i = 0; i < r.messages_size(); ++i) {
                    auto& item = r.messages(i);
                    time_t t = item.timestamp();
                    char time_buf[32];
                    strftime(time_buf, sizeof(time_buf), "%m-%d %H:%M", localtime(&t));
                    std::cout << "  [" << time_buf << "] " << item.sender_name()
                              << ": " << item.content() << std::endl;
                }
                if (r.messages_size() == 0) {
                    std::cout << "  (无消息)" << std::endl;
                }
            } else {
                std::cerr << "[失败] " << r.error_message() << std::endl;
            }
        }
    }

    static constexpr size_t FILE_CHUNK_SIZE = 64 * 1024;  // 64KB

    // ===== 文件发送给用户 (420-439) =====

    // 发送文件给指定用户
    bool send_file_to_user(uint64_t target_uid, const std::string& filepath) {
        // 校验：必须是普通文件
        struct stat st;
        if (stat(filepath.c_str(), &st) != 0) {
            std::cerr << "[错误] 无法访问文件: " << filepath << std::endl;
            return false;
        }
        if (!S_ISREG(st.st_mode)) {
            std::cerr << "[错误] 不是普通文件: " << filepath << std::endl;
            return false;
        }

        FILE* fp = fopen(filepath.c_str(), "rb");
        if (!fp) {
            std::cerr << "[错误] 无法打开文件: " << filepath << std::endl;
            return false;
        }
        fseek(fp, 0, SEEK_END);
        uint64_t fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        // 二次校验：文件大小异常
        if (fsize == 0 || fsize > 1024ULL * 1024 * 1024 * 10) {  // 最大 10GB
            std::cerr << "[错误] 文件大小异常: " << fsize << " bytes" << std::endl;
            fclose(fp); return false;
        }

        std::string filename = filepath;
        auto pos = filepath.find_last_of('/');
        if (pos != std::string::npos) filename = filepath.substr(pos + 1);

        uint32_t total_chunks = static_cast<uint32_t>(
            (fsize + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
        if (total_chunks == 0) total_chunks = 1;

        std::cout << "[信息] 发送文件 " << filename << " (" << fsize << " bytes, "
                  << total_chunks << " 分片) 给 user=" << target_uid << std::endl;

        // 计算完整文件哈希
        std::string full_file_data(fsize, '\0');
        fread(&full_file_data[0], 1, fsize, fp);
        fseek(fp, 0, SEEK_SET);
        std::string file_hash = sha256_hex(full_file_data);  // hex 编码，兼容 MySQL

        // Step 1: 发起文件发送请求
        uint64_t transfer_id = 0;
        {
            chatroom::ChatMessage msg;
            msg.set_type(static_cast<uint32_t>(MessageType::FILE_SEND_REQ));
            msg.set_token(token_);
            msg.set_sender_id(user_id_);
            msg.set_target_id(target_uid);
            msg.set_timestamp(time(nullptr));
            auto* body = msg.mutable_file_send_req();
            body->set_file_name(filename);
            body->set_file_size(fsize);
            body->set_total_chunks(total_chunks);
            body->set_file_hash(file_hash);

            if (!send_message(msg)) { fclose(fp); return false; }
            auto resp = wait_response(MessageType::FILE_SEND_RSP, 10);
            if (resp.type() != static_cast<uint32_t>(MessageType::FILE_SEND_RSP) ||
                !resp.has_file_send_rsp() || !resp.file_send_rsp().success()) {
                std::cerr << "[失败] " << (resp.has_file_send_rsp() ?
                    resp.file_send_rsp().error_message() : "无响应") << std::endl;
                fclose(fp); return false;
            }
            transfer_id = resp.file_send_rsp().transfer_id();
            std::cout << "[信息] 传输已创建, transfer_id=" << transfer_id << std::endl;
        }

        // Step 2: 查询已上传分片（续传）
        std::set<uint32_t> sent_set;
        {
            chatroom::ChatMessage status_msg;
            status_msg.set_type(static_cast<uint32_t>(MessageType::FILE_TRANSFER_STATUS_REQ));
            status_msg.set_token(token_);
            status_msg.set_sender_id(user_id_);
            status_msg.set_target_id(transfer_id);
            status_msg.set_timestamp(time(nullptr));
            status_msg.mutable_file_transfer_status_req()->set_transfer_id(transfer_id);

            if (send_message(status_msg)) {
                auto sr = wait_response(MessageType::FILE_TRANSFER_STATUS_RSP, 10);
                if (sr.type() == static_cast<uint32_t>(MessageType::FILE_TRANSFER_STATUS_RSP) &&
                    sr.has_file_transfer_status_rsp() && sr.file_transfer_status_rsp().success()) {
                    auto& r = sr.file_transfer_status_rsp();
                    for (int i = 0; i < r.sender_received_chunks_size(); ++i)
                        sent_set.insert(r.sender_received_chunks(i));
                    if (!sent_set.empty())
                        std::cout << "[续传] 已有 " << sent_set.size() << " 个分片" << std::endl;
                }
            }
        }

        // Step 3: 发送缺失的分片
        uint32_t sent = sent_set.size();
        for (uint32_t seq = 0; seq < total_chunks; ++seq) {
            if (sent_set.count(seq)) continue;

            uint64_t offset = static_cast<uint64_t>(seq) * FILE_CHUNK_SIZE;
            uint64_t chunk_size = std::min(static_cast<uint64_t>(FILE_CHUNK_SIZE), fsize - offset);
            std::string chunk_data(chunk_size, '\0');
            fseek(fp, offset, SEEK_SET);
            fread(&chunk_data[0], 1, chunk_size, fp);

            chatroom::ChatMessage chunk_msg;
            chunk_msg.set_type(static_cast<uint32_t>(MessageType::FILE_SEND_CHUNK_REQ));
            chunk_msg.set_token(token_);
            chunk_msg.set_sender_id(user_id_);
            chunk_msg.set_target_id(transfer_id);  // transfer_id in envelope
            chunk_msg.set_timestamp(time(nullptr));
            auto* cb = chunk_msg.mutable_file_send_chunk_req();
            cb->set_file_name(filename);
            cb->set_file_size(fsize);
            cb->set_file_data(chunk_data);
            cb->set_chunk_seq(seq);
            cb->set_total_chunks(total_chunks);
            cb->set_chunk_hash(sha256_hex(chunk_data));  // SHA-256 hex 编码

            if (!send_message(chunk_msg)) { fclose(fp); return false; }
            auto cr = wait_response(MessageType::FILE_SEND_CHUNK_RSP, 10);
            if (cr.type() != static_cast<uint32_t>(MessageType::FILE_SEND_CHUNK_RSP) ||
                !cr.file_send_chunk_rsp().success()) {
                std::cerr << "\n[错误] 分片 " << seq << " 发送失败" << std::endl;
                fclose(fp); return false;
            }
            sent++;
            std::cout << "\r[进度] " << sent << "/" << total_chunks
                      << " (" << (sent * 100 / total_chunks) << "%)" << std::flush;
        }

        fclose(fp);

        // Step 4: 发送 finalize 请求，服务端组装文件
        {
            chatroom::ChatMessage fin_msg;
            fin_msg.set_type(static_cast<uint32_t>(MessageType::FILE_FINALIZE_REQ));
            fin_msg.set_token(token_);
            fin_msg.set_sender_id(user_id_);
            fin_msg.set_target_id(transfer_id);  // transfer_id in envelope
            fin_msg.set_timestamp(time(nullptr));
            auto* fb = fin_msg.mutable_file_finalize_req();
            fb->set_transfer_id(transfer_id);
            fb->set_file_hash(file_hash);  // hex SHA-256

            if (!send_message(fin_msg)) {
                std::cerr << "[警告] finalize 请求发送失败" << std::endl;
                std::cout << std::endl << "[成功] 文件发送完成!" << std::endl;
                return true;
            }
            auto fr = wait_response(MessageType::FILE_FINALIZE_RSP, 30);
            if (fr.type() == static_cast<uint32_t>(MessageType::FILE_FINALIZE_RSP) &&
                fr.has_file_finalize_rsp() && fr.file_finalize_rsp().success()) {
                std::cout << std::endl << "[成功] 文件发送完成! 服务端已组装文件: "
                          << fr.file_finalize_rsp().final_path() << std::endl;
            } else {
                std::cerr << "[警告] finalize 响应失败，但分片已全部发送" << std::endl;
                std::cout << std::endl << "[成功] 文件发送完成!" << std::endl;
            }
        }

        return true;
    }

    // 接受/拒绝文件传输
    bool accept_transfer(uint64_t transfer_id, bool accept) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::FILE_TRANSFER_ACCEPT_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_target_id(transfer_id);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_file_transfer_accept_req();
        body->set_transfer_id(transfer_id);
        body->set_accept(accept);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::FILE_TRANSFER_ACCEPT_RSP, 10);
        if (resp.type() == static_cast<uint32_t>(MessageType::FILE_TRANSFER_ACCEPT_RSP) &&
            resp.has_file_transfer_accept_rsp()) {
            auto& r = resp.file_transfer_accept_rsp();
            if (r.success()) {
                std::cout << "[成功] " << (accept ? "已接受" : "已拒绝") << "文件传输" << std::endl;
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
        }
        return false;
    }

    // 接收文件（拉取分片）
    bool receive_file_chunks(uint64_t transfer_id, const std::string& save_path) {
        // 查询传输状态
        chatroom::ChatMessage status_msg;
        status_msg.set_type(static_cast<uint32_t>(MessageType::FILE_TRANSFER_STATUS_REQ));
        status_msg.set_token(token_);
        status_msg.set_sender_id(user_id_);
        status_msg.set_target_id(transfer_id);
        status_msg.set_timestamp(time(nullptr));
        status_msg.mutable_file_transfer_status_req()->set_transfer_id(transfer_id);

        if (!send_message(status_msg)) return false;
        auto sr = wait_response(MessageType::FILE_TRANSFER_STATUS_RSP, 10);
        if (sr.type() != static_cast<uint32_t>(MessageType::FILE_TRANSFER_STATUS_RSP) ||
            !sr.has_file_transfer_status_rsp() || !sr.file_transfer_status_rsp().success()) {
            std::cerr << "[失败] 无法查询传输状态" << std::endl;
            return false;
        }

        auto& r = sr.file_transfer_status_rsp();
        std::string fname = r.file_name();
        uint64_t fsize = r.file_size();
        uint32_t total_chunks = r.total_chunks();

        std::string out_path = save_path.empty() ? fname : save_path;

        // 如果保存路径是目录，在目录下创建同名文件
        struct stat path_st;
        if (stat(out_path.c_str(), &path_st) == 0 && S_ISDIR(path_st.st_mode)) {
            if (out_path.back() != '/') out_path += '/';
            out_path += fname;
            std::cout << "[信息] 保存到目录: " << out_path << std::endl;
        }

        std::cout << "[信息] 接收文件: " << fname << " (" << fsize << " bytes, "
                  << total_chunks << " 分片)" << std::endl;

        // 已有的分片（续传）
        std::set<uint32_t> have_set;
        for (int i = 0; i < r.receiver_received_chunks_size(); ++i)
            have_set.insert(r.receiver_received_chunks(i));

        if (!have_set.empty())
            std::cout << "[续传] 已有 " << have_set.size() << " 个分片" << std::endl;

        // 打开文件（续传模式：不截断已有数据）
        FILE* fp = fopen(out_path.c_str(), "r+b");
        if (!fp) fp = fopen(out_path.c_str(), "wb");
        if (!fp) {
            std::cerr << "[错误] 无法创建文件: " << out_path << std::endl;
            return false;
        }

        // 拉取缺失的分片
        uint32_t received = have_set.size();
        for (uint32_t seq = 0; seq < total_chunks; ++seq) {
            if (have_set.count(seq)) continue;

            chatroom::ChatMessage chunk_msg;
            chunk_msg.set_type(static_cast<uint32_t>(MessageType::FILE_RECEIVE_CHUNK_REQ));
            chunk_msg.set_token(token_);
            chunk_msg.set_sender_id(user_id_);
            chunk_msg.set_target_id(transfer_id);
            chunk_msg.set_timestamp(time(nullptr));
            chunk_msg.mutable_file_receive_chunk_req()->set_transfer_id(transfer_id);
            chunk_msg.mutable_file_receive_chunk_req()->set_chunk_seq(seq);

            if (!send_message(chunk_msg)) { fclose(fp); return false; }
            auto cr = wait_response(MessageType::FILE_RECEIVE_CHUNK_RSP, 30);
            if (cr.type() != static_cast<uint32_t>(MessageType::FILE_RECEIVE_CHUNK_RSP) ||
                !cr.has_file_receive_chunk_rsp()) {
                std::cerr << "\n[错误] 分片 " << seq << " 接收失败" << std::endl;
                fclose(fp); return false;
            }

            auto& chunk_r = cr.file_receive_chunk_rsp();

            uint64_t offset = static_cast<uint64_t>(seq) * FILE_CHUNK_SIZE;
            fseek(fp, offset, SEEK_SET);
            fwrite(chunk_r.file_data().data(), 1, chunk_r.file_data().size(), fp);
            received++;

            std::cout << "\r[进度] " << received << "/" << total_chunks
                      << " (" << (received * 100 / total_chunks) << "%)" << std::flush;
        }

        fclose(fp);

        // Step: 发送 finalize 请求，服务端组装文件
        {
            chatroom::ChatMessage fin_msg;
            fin_msg.set_type(static_cast<uint32_t>(MessageType::FILE_FINALIZE_REQ));
            fin_msg.set_token(token_);
            fin_msg.set_sender_id(user_id_);
            fin_msg.set_target_id(transfer_id);
            fin_msg.set_timestamp(time(nullptr));
            auto* fb = fin_msg.mutable_file_finalize_req();
            fb->set_transfer_id(transfer_id);
            fb->set_file_hash(r.file_hash());  // hex SHA-256 string

            if (!send_message(fin_msg)) {
                std::cerr << "[警告] finalize 请求发送失败" << std::endl;
                std::cout << std::endl << "[成功] 文件已保存到: " << out_path << std::endl;
                return true;
            }
            auto fr = wait_response(MessageType::FILE_FINALIZE_RSP, 30);
            if (fr.type() == static_cast<uint32_t>(MessageType::FILE_FINALIZE_RSP) &&
                fr.has_file_finalize_rsp() && fr.file_finalize_rsp().success()) {
                std::cout << std::endl << "[成功] 文件接收完成! 服务端已组装: "
                          << fr.file_finalize_rsp().final_path() << std::endl;
            } else {
                std::cerr << "[警告] finalize 响应失败" << std::endl;
                std::cout << std::endl << "[成功] 文件已保存到: " << out_path << std::endl;
            }
        }

        return true;
    }

    // 获取缓存数据
    const std::vector<chatroom::FriendInfo>& cached_friends() const { return friend_cache_; }
    const std::vector<chatroom::GroupInfo>& cached_groups() const { return group_cache_; }
    const std::vector<uint64_t>& pending_transfers() const { return pending_transfers_; }
    void clear_pending_transfers() { pending_transfers_.clear(); }

    // 查看本地 SQLite 聊天记录（chat_type: 0=私聊, 1=群聊）
    void show_local_history(int chat_type, uint64_t peer_id, int limit = 50) {
        local_store_.print_history(chat_type, peer_id, limit);
    }

private:
    // ===== 接收循环（后台线程）=====
    void recv_loop() {
        std::string recv_buf;
        uint32_t expected_len = 0;
        bool reading_header = true;

        while (running_.load()) {
            char tmp[4096];
            ssize_t n = recv(sockfd_, tmp, sizeof(tmp), 0);
            if (n <= 0) {
                if (running_.load()) {
                    std::cerr << "\n[错误] 与服务端的连接断开" << std::endl;
                }
                running_.store(false);
                resp_cv_.notify_all();
                break;
            }

            recv_buf.append(tmp, n);

            // 循环解析帧
            while (true) {
                if (reading_header) {
                    if (recv_buf.size() < 4) break;
                    uint32_t net_len;
                    memcpy(&net_len, recv_buf.data(), 4);
                    expected_len = ntohl(net_len);
                    recv_buf.erase(0, 4);
                    reading_header = false;
                }

                if (!reading_header) {
                    if (recv_buf.size() < expected_len) break;

                    std::string payload = recv_buf.substr(0, expected_len);
                    recv_buf.erase(0, expected_len);
                    reading_header = true;

                    process_payload(std::move(payload));
                }
            }
        }
    }

    void process_payload(std::string payload) {
        // 尝试解析为 Protobuf ChatMessage
        chatroom::ChatMessage msg;
        if (msg.ParseFromString(payload)) {
            std::lock_guard<std::mutex> lock(resp_mutex_);

            uint32_t mtype = msg.type();

            // 服务端推送的文件传输通知
            if (mtype == static_cast<uint32_t>(MessageType::FILE_TRANSFER_NOTIFY) &&
                msg.sender_id() != user_id_) {
                if (msg.has_file_transfer_notify()) {
                    auto& n = msg.file_transfer_notify();
                    std::string info = "[文件传输] " + n.sender_name()
                        + " 向你发送文件: " + n.file_name()
                        + " (" + std::to_string(n.file_size()) + " bytes, "
                        + std::to_string(n.total_chunks()) + " 分片)"
                        + " [transfer_id=" + std::to_string(n.transfer_id()) + "]";
                    notifications_.push_back(info);
                    // 同时记录 transfer_id 供后续使用
                    pending_transfers_.push_back(n.transfer_id());
                }
                return;
            }

            // 服务端推送的私聊/群聊消息：sender_id != 自己
            if ((mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP) ||
                 mtype == static_cast<uint32_t>(MessageType::GROUP_CHAT_RSP)) &&
                msg.sender_id() != user_id_) {
                std::string kind = (mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP))
                                       ? "[私聊]" : "[群聊]";
                std::string sender_name;
                std::string content;
                if (mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP) &&
                    msg.has_private_chat_rsp()) {
                    sender_name = msg.private_chat_rsp().sender_name();
                    content = msg.private_chat_rsp().content();
                } else if (mtype == static_cast<uint32_t>(MessageType::GROUP_CHAT_RSP) &&
                           msg.has_group_chat_rsp()) {
                    sender_name = msg.group_chat_rsp().sender_name();
                    content = msg.group_chat_rsp().content();
                }
                if (sender_name.empty()) sender_name = std::to_string(msg.sender_id());
                // 落盘到本地 SQLite
                if (mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP)) {
                    local_store_.save_message(0, msg.sender_id(), msg.sender_id(),
                                              sender_name, content, msg.timestamp(), false);
                } else {
                    local_store_.save_message(1, msg.group_id(), msg.sender_id(),
                                              sender_name, content, msg.timestamp(), false);
                }
                notifications_.push_back(kind + " " + sender_name + ": " + content);
                return;
            }

            // 正常的请求-响应或未知消息
            responses_.push_back(std::move(msg));
            resp_cv_.notify_all();
        } else {
            // 不是 Protobuf，当作纯文本通知（如离线消息、好友上线等）
            std::lock_guard<std::mutex> lock(resp_mutex_);
            notifications_.push_back(std::move(payload));
        }
    }

    // ===== 辅助函数 =====

    bool handle_friend_op_resp(const chatroom::ChatMessage& resp, const std::string& op_name) {
        // 检查各个可能的 friend 响应字段
        auto check_resp = [&](const chatroom::FriendOpResponse& r) -> bool {
            if (r.success()) {
                std::cout << "[成功] " << op_name << "成功!" << std::endl;
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
            return false;
        };

        if (resp.has_add_friend_rsp() && resp.add_friend_rsp().success()) {
            return check_resp(resp.add_friend_rsp());
        }
        if (resp.has_delete_friend_rsp()) {
            return check_resp(resp.delete_friend_rsp());
        }
        if (resp.has_block_friend_rsp()) {
            return check_resp(resp.block_friend_rsp());
        }
        if (resp.has_unblock_friend_rsp()) {
            return check_resp(resp.unblock_friend_rsp());
        }

        std::cerr << "[错误] " << op_name << ": 未收到有效响应" << std::endl;
        return false;
    }

    bool handle_group_op_resp(const chatroom::ChatMessage& resp, const std::string& op_name) {
        auto check = [&](const chatroom::GroupOpResponse& r) -> bool {
            if (r.success()) {
                std::cout << "[成功] " << op_name << "成功!" << std::endl;
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
            return false;
        };

        if (resp.has_join_group_rsp()) return check(resp.join_group_rsp());
        if (resp.has_quit_group_rsp()) return check(resp.quit_group_rsp());
        if (resp.has_dismiss_group_rsp()) return check(resp.dismiss_group_rsp());
        if (resp.has_add_group_admin_rsp()) return check(resp.add_group_admin_rsp());
        if (resp.has_remove_group_admin_rsp()) return check(resp.remove_group_admin_rsp());
        if (resp.has_approve_join_group_rsp()) return check(resp.approve_join_group_rsp());
        if (resp.has_remove_group_member_rsp()) return check(resp.remove_group_member_rsp());
        if (resp.has_reject_join_group_rsp()) return check(resp.reject_join_group_rsp());

        std::cerr << "[错误] " << op_name << ": 未收到有效响应" << std::endl;
        return false;
    }

    bool handle_chat_resp(const chatroom::ChatMessage& resp, const std::string& op_name) {
        if (resp.has_private_chat_rsp()) {
            auto& r = resp.private_chat_rsp();
            if (r.success()) {
                std::cout << "[成功] " << op_name << "成功!" << std::endl;
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
        }
        if (resp.has_group_chat_rsp()) {
            auto& r = resp.group_chat_rsp();
            if (r.success()) {
                std::cout << "[成功] " << op_name << "成功!" << std::endl;
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
        }
        return false;
    }

    // ===== 成员变量 =====
    int sockfd_ = -1;
    std::atomic<bool> running_{false};
    std::thread recv_thread_;

    uint64_t user_id_ = 0;
    std::string username_;
    std::string token_;

    std::mutex resp_mutex_;
    std::condition_variable resp_cv_;
    std::deque<chatroom::ChatMessage> responses_;
    std::deque<std::string> notifications_;

    std::vector<chatroom::FriendInfo> friend_cache_;
    std::vector<chatroom::GroupInfo> group_cache_;
    std::vector<uint64_t> pending_transfers_;  // 待处理的文件传输 ID

    LocalStore local_store_;  // 本地 SQLite 聊天记录
};


// ============================================================
// 终端 UI 辅助函数
// ============================================================

void clear_screen() {
    std::cout << "\033[2J\033[H" << std::flush;
}

void print_header(const std::string& title) {
    std::cout << "\n========== " << title << " ==========" << std::endl;
}

void print_prompt() {
    std::cout << "> " << std::flush;
}

int read_choice(int max_choice) {
    while (true) {
        print_prompt();
        int choice;
        std::cin >> choice;
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cerr << "[错误] 请输入数字" << std::endl;
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (choice >= 0 && choice <= max_choice) {
            return choice;
        }
        std::cerr << "[错误] 请输入 0-" << max_choice << " 之间的数字" << std::endl;
    }
}

std::string read_line(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

uint64_t read_uint64(const std::string& prompt) {
    while (true) {
        std::cout << prompt << std::flush;
        uint64_t val;
        std::cin >> val;
        if (!std::cin.fail()) {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return val;
        }
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cerr << "[错误] 请输入有效的数字" << std::endl;
    }
}

// ============================================================
// 子菜单
// ============================================================

void menu_friend(ChatClient& client) {
    while (client.is_connected() && client.is_logged_in()) {
        print_header("好友管理");
        std::cout << "  1. 查看好友列表" << std::endl;
        std::cout << "  2. 添加好友" << std::endl;
        std::cout << "  3. 删除好友" << std::endl;
        std::cout << "  4. 拉黑好友" << std::endl;
        std::cout << "  5. 解除拉黑" << std::endl;
        std::cout << "  6. 查看已拉黑用户" << std::endl;
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(6);
        switch (choice) {
            case 0: return;
            case 1: client.query_friends(); break;
            case 2: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                client.add_friend(uid);
                break;
            }
            case 3: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                client.delete_friend(uid);
                break;
            }
            case 4: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                client.block_friend(uid);
                break;
            }
            case 5: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                client.unblock_friend(uid);
                break;
            }
            case 6:
                client.query_blocked();
                break;
        }
        client.flush_notifications();
    }
}

void menu_group(ChatClient& client) {
    while (client.is_connected() && client.is_logged_in()) {
        print_header("群组管理");
        std::cout << "  1. 查看群组列表" << std::endl;
        std::cout << "  2. 创建群组" << std::endl;
        std::cout << "  3. 加入群组" << std::endl;
        std::cout << "  4. 退出群组" << std::endl;
        std::cout << "  5. 查看群成员" << std::endl;
        std::cout << "  6. 添加管理员" << std::endl;
        std::cout << "  7. 移除管理员" << std::endl;
        std::cout << "  8. 批准加入" << std::endl;
        std::cout << "  9. 移除成员" << std::endl;
        std::cout << "  10. 拒绝加入" << std::endl;
        std::cout << "  11. 解散群组" << std::endl;
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(11);
        switch (choice) {
            case 0: return;
            case 1: client.query_group_list(); break;
            case 2: {
                std::string name = read_line("  群组名称: ");
                std::string desc = read_line("  群组描述: ");
                std::string pub = read_line("  是否公开? (y/n): ");
                client.create_group(name, desc, (pub == "y" || pub == "Y"));
                break;
            }
            case 3: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                client.join_group(gid);
                break;
            }
            case 4: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                client.quit_group(gid);
                break;
            }
            case 5: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                client.query_group_members(gid);
                break;
            }
            case 6: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                uint64_t uid = read_uint64("  请输入目标 user_id: ");
                client.add_group_admin(gid, uid);
                break;
            }
            case 7: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                uint64_t uid = read_uint64("  请输入目标 user_id: ");
                client.remove_group_admin(gid, uid);
                break;
            }
            case 8: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                uint64_t uid = read_uint64("  请输入目标 user_id: ");
                client.approve_join_group(gid, uid);
                break;
            }
            case 9: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                uint64_t uid = read_uint64("  请输入目标 user_id: ");
                client.remove_group_member(gid, uid);
                break;
            }
            case 10: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                uint64_t uid = read_uint64("  请输入目标 user_id: ");
                client.reject_join_group(gid, uid);
                break;
            }
            case 11: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                client.dismiss_group(gid);
                break;
            }
        }
        client.flush_notifications();
    }
}

void menu_chat(ChatClient& client) {
    while (client.is_connected() && client.is_logged_in()) {
        print_header("聊天");
        std::cout << "  1. 私聊（发送消息）" << std::endl;
        std::cout << "  2. 群聊（发送消息）" << std::endl;
        std::cout << "  3. 查看私聊历史" << std::endl;
        std::cout << "  4. 查看群聊历史" << std::endl;
        std::cout << "  5. 查看本地聊天记录" << std::endl;
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(5);
        switch (choice) {
            case 0: return;
            case 1: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                std::string text = read_line("  消息内容: ");
                client.send_private_chat(uid, text);
                break;
            }
            case 2: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                std::string text = read_line("  消息内容: ");
                client.send_group_chat(gid, text);
                break;
            }
            case 3: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                client.get_history(uid, 0, 50);
                break;
            }
            case 4: {
                uint64_t gid = read_uint64("  请输入 group_id: ");
                client.get_history(0, gid, 50);
                break;
            }
            case 5: {
                std::string t = read_line("  类型 (1=私聊, 2=群聊): ");
                if (t == "1") {
                    uint64_t uid = read_uint64("  请输入对方 user_id: ");
                    std::cout << "\n===== 本地私聊记录 =====" << std::endl;
                    client.show_local_history(0, uid, 50);
                } else if (t == "2") {
                    uint64_t gid = read_uint64("  请输入 group_id: ");
                    std::cout << "\n===== 本地群聊记录 =====" << std::endl;
                    client.show_local_history(1, gid, 50);
                }
                break;
            }
        }
        client.flush_notifications();
    }
}

void menu_file(ChatClient& client) {
    while (client.is_connected() && client.is_logged_in()) {
        print_header("文件传输");
        std::cout << "  1. 发送文件给用户" << std::endl;
        std::cout << "  2. 接收待处理文件" << std::endl;
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(2);
        switch (choice) {
            case 0: return;
            case 1: {
                uint64_t uid = read_uint64("  请输入对方 user_id: ");
                std::string path = read_line("  文件路径: ");
                client.send_file_to_user(uid, path);
                break;
            }
            case 2: {
                client.flush_notifications();
                auto pending = client.pending_transfers();
                if (pending.empty()) {
                    std::cout << "  (没有待处理的文件)" << std::endl;
                    break;
                }
                std::cout << "  待处理的 transfer_id: ";
                for (size_t i = 0; i < pending.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << pending[i];
                }
                std::cout << std::endl;
                uint64_t tid = read_uint64("  请输入 transfer_id (0=取消): ");
                if (tid == 0) break;
                // 校验输入是否在待处理列表中，避免手输错误导致“传输记录不存在”
                bool found = false;
                for (auto t : pending) if (t == tid) { found = true; break; }
                if (!found) {
                    std::cerr << "[错误] 该 transfer_id 不在待处理列表中" << std::endl;
                    break;
                }
                std::string yn = read_line("  是否接受? (y/n): ");
                if (yn != "y" && yn != "Y") {
                    client.accept_transfer(tid, false);
                    break;
                }
                if (client.accept_transfer(tid, true)) {
                    std::string save = read_line("  保存路径 (回车=默认): ");
                    client.receive_file_chunks(tid, save);
                }
                break;
            }
        }
        client.flush_notifications();
    }
}

// ============================================================
// 主菜单
// ============================================================

void main_menu(ChatClient& client) {
    while (client.is_connected()) {
        print_header(client.is_logged_in()
                         ? "主菜单 (已登录: " + client.username() + ")"
                         : "主菜单 (未登录)");

        if (!client.is_logged_in()) {
            std::cout << "  1. 登录" << std::endl;
            std::cout << "  2. 注册" << std::endl;
            std::cout << "  0. 退出" << std::endl;

            int choice = read_choice(2);
            switch (choice) {
                case 0:
                    client.disconnect();
                    return;
                case 1: {
                    std::string user = read_line("  用户名: ");
                    std::string pass = read_line("  密码: ");
                    client.login(user, pass);
                    break;
                }
                case 2: {
                    std::string user = read_line("  用户名: ");
                    std::string pass = read_line("  密码: ");
                    std::string nick = read_line("  昵称: ");
                    client.register_user(user, pass, nick);
                    break;
                }
            }
        } else {
            std::cout << "  1. 好友管理" << std::endl;
            std::cout << "  2. 群组管理" << std::endl;
            std::cout << "  3. 聊天" << std::endl;
            std::cout << "  4. 文件传输" << std::endl;
            std::cout << "  5. 登出" << std::endl;
            std::cout << "  6. 注销账号" << std::endl;
            std::cout << "  0. 退出" << std::endl;

            int choice = read_choice(6);
            switch (choice) {
                case 0:
                    client.logout();
                    client.disconnect();
                    return;
                case 1: menu_friend(client); break;
                case 2: menu_group(client); break;
                case 3: menu_chat(client); break;
                case 4: menu_file(client); break;
                case 5:
                    client.logout();
                    break;
                case 6: {
                    std::string pass = read_line("  请输入密码确认注销: ");
                    if (client.delete_account(pass)) {
                        return; // 注销后返回登录界面
                    }
                    break;
                }
            }
        }
        client.flush_notifications();
    }
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8080;

    // 从配置文件读取（若存在），命令行参数仍可覆盖
    chatroom::Config config;
    if (config.load("chatroom.conf")) {
        host = config.get("client", "host", host);
        port = config.getInt("client", "port", port);
    }

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    std::cout << "╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║       ChatRoom 命令行测试客户端           ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝" << std::endl;
    std::cout << "  服务端: " << host << ":" << port << std::endl;
    std::cout << "  用法: " << argv[0] << " [host] [port]" << std::endl;
    std::cout << std::endl;

    ChatClient client;

    if (!client.connect(host, port)) {
        return 1;
    }

    main_menu(client);

    std::cout << "[信息] 客户端已退出" << std::endl;
    return 0;
}
