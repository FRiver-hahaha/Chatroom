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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using chatroom::MessageType; // 简化枚举使用

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
        return handle_chat_resp(resp, "发送私聊");
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
        return handle_chat_resp(resp, "发送群聊");
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

    // --- 文件 ---

    bool upload_file(const std::string& filepath) {
        // 读取文件内容
        FILE* fp = fopen(filepath.c_str(), "rb");
        if (!fp) {
            std::cerr << "[错误] 无法打开文件: " << filepath << std::endl;
            return false;
        }
        fseek(fp, 0, SEEK_END);
        uint64_t fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        std::string filedata(fsize, '\0');
        fread(&filedata[0], 1, fsize, fp);
        fclose(fp);

        // 提取文件名
        std::string filename = filepath;
        auto pos = filepath.find_last_of('/');
        if (pos != std::string::npos) filename = filepath.substr(pos + 1);

        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::FILE_UPLOAD_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        auto* body = msg.mutable_file_upload_req();
        body->set_file_name(filename);
        body->set_file_size(fsize);
        body->set_file_data(filedata);
        body->set_chunk_seq(0);
        body->set_total_chunks(1);

        std::cout << "[信息] 上传文件: " << filename << " (" << fsize << " bytes)" << std::endl;

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::FILE_UPLOAD_RSP, 30);
        if (resp.type() == static_cast<uint32_t>(MessageType::FILE_UPLOAD_RSP) && resp.has_file_upload_rsp()) {
            auto& r = resp.file_upload_rsp();
            if (r.success()) {
                std::cout << "[成功] 文件上传成功! file_id=" << r.file_id() << std::endl;
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
        }
        return false;
    }

    bool download_file(uint64_t file_id, const std::string& /*save_path*/) {
        chatroom::ChatMessage msg;
        msg.set_type(static_cast<uint32_t>(MessageType::FILE_DOWNLOAD_REQ));
        msg.set_token(token_);
        msg.set_sender_id(user_id_);
        msg.set_timestamp(time(nullptr));
        msg.mutable_file_download_req()->set_file_id(file_id);

        if (!send_message(msg)) return false;
        auto resp = wait_response(MessageType::FILE_DOWNLOAD_RSP, 30);
        if (resp.type() == static_cast<uint32_t>(MessageType::FILE_DOWNLOAD_RSP) && resp.has_file_download_rsp()) {
            auto& r = resp.file_download_rsp();
            if (r.success()) {
                std::cout << "[成功] 文件下载请求已发送: " << r.file_name()
                          << " (" << r.file_size() << " bytes)" << std::endl;
                // 注意：实际文件内容通过 file_upload_req 中的 file_data 字段传回
                // 这里简化为接受响应即可
                return true;
            }
            std::cerr << "[失败] " << r.error_message() << std::endl;
        }
        return false;
    }

    // 获取缓存数据
    const std::vector<chatroom::FriendInfo>& cached_friends() const { return friend_cache_; }
    const std::vector<chatroom::GroupInfo>& cached_groups() const { return group_cache_; }

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

            // 服务端推送的私聊/群聊消息：sender_id != 自己
            if ((mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP) ||
                 mtype == static_cast<uint32_t>(MessageType::GROUP_CHAT_RSP)) &&
                msg.sender_id() != user_id_) {
                std::string kind = (mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP))
                                       ? "[私聊]" : "[群聊]";
                notifications_.push_back(kind + " 来自 user=" +
                                          std::to_string(msg.sender_id()) +
                                          " group=" + std::to_string(msg.group_id()));
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
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(5);
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
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(9);
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
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(4);
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
        }
        client.flush_notifications();
    }
}

void menu_file(ChatClient& client) {
    while (client.is_connected() && client.is_logged_in()) {
        print_header("文件传输");
        std::cout << "  1. 上传文件" << std::endl;
        std::cout << "  2. 下载文件" << std::endl;
        std::cout << "  0. 返回上级" << std::endl;

        int choice = read_choice(2);
        switch (choice) {
            case 0: return;
            case 1: {
                std::string path = read_line("  文件路径: ");
                client.upload_file(path);
                break;
            }
            case 2: {
                uint64_t fid = read_uint64("  请输入 file_id: ");
                std::string path = read_line("  保存路径: ");
                client.download_file(fid, path);
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
