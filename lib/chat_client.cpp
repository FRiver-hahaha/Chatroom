/**
 * ChatRoom 客户端网络库 实现
 *
 * 从 client/client.cpp 提取的网络层和业务 API 实现
 * 关键改动：wait_response() 阻塞模式 → 异步回调分发
 */

#include "lib/chat_client.hpp"

#include <iostream>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace chatroom {

// ===== 构造 / 析构 =====

ChatClient::ChatClient() = default;

ChatClient::~ChatClient() {
    disconnect();
}

// ===== 回调注册 =====

void ChatClient::set_notification_callback(NotificationCallback cb) {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    notification_cb_ = std::move(cb);
}

void ChatClient::set_log_callback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    log_cb_ = std::move(cb);
}

void ChatClient::log(const std::string& msg, int level) {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    if (log_cb_) {
        log_cb_(msg, level);
    } else {
        if (level >= 2) std::cerr << msg << std::endl;
        else std::cout << msg << std::endl;
    }
}

// ===== 连接管理 =====

bool ChatClient::connect(const std::string& host, int port) {
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        log("[错误] 创建 socket 失败", 2);
        return false;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        log("[错误] 无效的地址: " + host, 2);
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    if (::connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log(std::string("[错误] 连接失败: ") + strerror(errno), 2);
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    running_.store(true);
    recv_thread_ = std::thread(&ChatClient::recv_loop, this);

    log("[信息] 已连接到 " + host + ":" + std::to_string(port));
    return true;
}

void ChatClient::disconnect() {
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

bool ChatClient::is_connected() const {
    return sockfd_ >= 0 && running_.load();
}

// ===== 发送 =====

bool ChatClient::send_message(const ChatMessage& msg) {
    std::string payload;
    if (!msg.SerializeToString(&payload)) {
        log("[错误] Protobuf 序列化失败", 2);
        return false;
    }
    return send_raw(payload);
}

bool ChatClient::send_raw(const std::string& data) {
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    std::string packet(4, '\0');
    memcpy(&packet[0], &len, 4);
    packet.append(data);

    size_t sent = 0;
    while (sent < packet.size()) {
        ssize_t n = ::send(sockfd_, packet.data() + sent, packet.size() - sent, 0);
        if (n <= 0) {
            log(std::string("[错误] 发送失败: ") + strerror(errno), 2);
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// ===== 异步请求（核心改动：替代 wait_response）=====

void ChatClient::send_request(const ChatMessage& msg, MessageType expected_response,
                               ResponseCallback on_response) {
    // 注册回调
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        response_callbacks_[static_cast<uint32_t>(expected_response)] = std::move(on_response);
    }
    send_message(msg);
}

// ===== 接收循环（后台线程）=====

void ChatClient::recv_loop() {
    std::string recv_buf;
    uint32_t expected_len = 0;
    bool reading_header = true;

    while (running_.load()) {
        char tmp[4096];
        ssize_t n = recv(sockfd_, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            if (running_.load()) {
                log("[错误] 与服务端的连接断开", 2);
            }
            running_.store(false);
            break;
        }

        recv_buf.append(tmp, static_cast<size_t>(n));

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

void ChatClient::process_payload(std::string payload) {
    ChatMessage msg;
    if (msg.ParseFromString(payload)) {
        uint32_t mtype = msg.type();

        // 服务端推送的私聊/群聊消息（sender_id != 自己）
        if ((mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP) ||
             mtype == static_cast<uint32_t>(MessageType::GROUP_CHAT_RSP)) &&
            msg.sender_id() != user_id_) {

            std::string kind = (mtype == static_cast<uint32_t>(MessageType::PRIVATE_CHAT_RSP))
                                   ? "[私聊]" : "[群聊]";
            std::string text = kind + " 来自 user=" + std::to_string(msg.sender_id());

            // 如果有消息正文，提取出来
            if (msg.has_private_chat_rsp()) {
                // 推送消息：ChatMessage 本身作为信封
                text = msg.private_chat_rsp().ShortDebugString();
            }

            // 通知回调
            std::lock_guard<std::mutex> lock(notify_mutex_);
            if (notification_cb_) {
                notification_cb_(payload); // 把原始 protobuf 数据传给上层解析
            }
            return;
        }

        // 查找注册的回调
        ResponseCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            auto it = response_callbacks_.find(mtype);
            if (it != response_callbacks_.end()) {
                cb = std::move(it->second);
                response_callbacks_.erase(it);
            }
        }

        if (cb) {
            cb(msg);

            // 更新缓存（登录成功时）
            if (mtype == static_cast<uint32_t>(MessageType::LOGIN_RSP) &&
                msg.has_login_rsp() && msg.login_rsp().success()) {
                user_id_ = msg.login_rsp().user_id();
                username_ = msg.login_rsp().username();
                nickname_ = msg.login_rsp().nickname();
                token_ = msg.login_rsp().token();
            }

            // 登出/注销时清空会话
            if ((mtype == static_cast<uint32_t>(MessageType::LOGOUT_RSP) &&
                 msg.has_logout_rsp() && msg.logout_rsp().success()) ||
                (mtype == static_cast<uint32_t>(MessageType::DELETE_ACCOUNT_RSP) &&
                 msg.has_delete_account_rsp() && msg.delete_account_rsp().success())) {
                user_id_ = 0;
                username_.clear();
                nickname_.clear();
                token_.clear();
            }

            // 更新好友缓存
            if (mtype == static_cast<uint32_t>(MessageType::QUERY_FRIEND_RSP) &&
                msg.has_query_friend_rsp() && msg.query_friend_rsp().success()) {
                friend_cache_.clear();
                auto& r = msg.query_friend_rsp();
                for (int i = 0; i < r.friends_size(); ++i) {
                    friend_cache_.push_back(r.friends(i));
                }
            }

            // 更新群组缓存
            if (mtype == static_cast<uint32_t>(MessageType::QUERY_GROUP_LIST_RSP) &&
                msg.has_query_group_list_rsp() && msg.query_group_list_rsp().success()) {
                group_cache_.clear();
                auto& r = msg.query_group_list_rsp();
                for (int i = 0; i < r.groups_size(); ++i) {
                    group_cache_.push_back(r.groups(i));
                }
            }
        }
        // 如果没有回调注册，忽略该消息
    } else {
        // 不是 Protobuf，当作纯文本通知
        std::lock_guard<std::mutex> lock(notify_mutex_);
        if (notification_cb_) {
            notification_cb_(payload);
        }
    }
}

// ===== 会话状态 =====

bool ChatClient::is_logged_in() const { return !token_.empty(); }
uint64_t ChatClient::user_id() const { return user_id_; }
const std::string& ChatClient::username() const { return username_; }
const std::string& ChatClient::token() const { return token_; }
const std::string& ChatClient::nickname() const { return nickname_; }

const std::vector<chatroom::FriendInfo>& ChatClient::cached_friends() const { return friend_cache_; }
const std::vector<chatroom::GroupInfo>& ChatClient::cached_groups() const { return group_cache_; }

// ============================================================
// 业务 API 实现
// ============================================================

// --- 账号模块 ---

void ChatClient::login(const std::string& user, const std::string& pass,
                        ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::LOGIN_REQ));
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_login_req();
    body->set_username(user);
    body->set_password(pass);
    send_request(msg, MessageType::LOGIN_RSP, std::move(cb));
}

void ChatClient::register_user(const std::string& user, const std::string& pass,
                                const std::string& nick, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::REGISTER_REQ));
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_register_req();
    body->set_username(user);
    body->set_password(pass);
    body->set_nickname(nick);
    send_request(msg, MessageType::REGISTER_RSP, std::move(cb));
}

void ChatClient::logout(ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::LOGOUT_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_logout_req();
    send_request(msg, MessageType::LOGOUT_RSP, std::move(cb));
}

void ChatClient::delete_account(const std::string& password, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::DELETE_ACCOUNT_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_delete_account_req()->set_password(password);
    send_request(msg, MessageType::DELETE_ACCOUNT_RSP, std::move(cb));
}

// --- 好友模块 ---

void ChatClient::add_friend(uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::ADD_FRIEND_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_add_friend_req()->set_target_user_id(target_uid);
    send_request(msg, MessageType::ADD_FRIEND_RSP, std::move(cb));
}

void ChatClient::delete_friend(uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::DELETE_FRIEND_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_delete_friend_req()->set_target_user_id(target_uid);
    send_request(msg, MessageType::DELETE_FRIEND_RSP, std::move(cb));
}

void ChatClient::block_friend(uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::BLOCK_FRIEND_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_block_friend_req()->set_target_user_id(target_uid);
    send_request(msg, MessageType::BLOCK_FRIEND_RSP, std::move(cb));
}

void ChatClient::unblock_friend(uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::UNBLOCK_FRIEND_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_unblock_friend_req()->set_target_user_id(target_uid);
    send_request(msg, MessageType::UNBLOCK_FRIEND_RSP, std::move(cb));
}

void ChatClient::query_friends(ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::QUERY_FRIEND_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_query_friend_req();
    send_request(msg, MessageType::QUERY_FRIEND_RSP, std::move(cb));
}

// --- 群组模块 ---

void ChatClient::create_group(const std::string& name, const std::string& desc,
                               bool is_public, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::CREATE_GROUP_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_create_group_req();
    body->set_group_name(name);
    body->set_description(desc);
    body->set_is_public(is_public);
    send_request(msg, MessageType::CREATE_GROUP_RSP, std::move(cb));
}

void ChatClient::join_group(uint64_t gid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::JOIN_GROUP_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_join_group_req()->set_group_id(gid);
    send_request(msg, MessageType::JOIN_GROUP_RSP, std::move(cb));
}

void ChatClient::quit_group(uint64_t gid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::QUIT_GROUP_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_quit_group_req()->set_group_id(gid);
    send_request(msg, MessageType::QUIT_GROUP_RSP, std::move(cb));
}

void ChatClient::query_group_list(ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::QUERY_GROUP_LIST_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_query_group_list_req();
    send_request(msg, MessageType::QUERY_GROUP_LIST_RSP, std::move(cb));
}

void ChatClient::query_group_members(uint64_t gid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::QUERY_GROUP_MEMBERS_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_query_group_members_req()->set_group_id(gid);
    send_request(msg, MessageType::QUERY_GROUP_MEMBERS_RSP, std::move(cb));
}

void ChatClient::add_group_admin(uint64_t gid, uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::ADD_GROUP_ADMIN_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_add_group_admin_req();
    body->set_group_id(gid);
    body->set_target_user_id(target_uid);
    send_request(msg, MessageType::ADD_GROUP_ADMIN_RSP, std::move(cb));
}

void ChatClient::remove_group_admin(uint64_t gid, uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::REMOVE_GROUP_ADMIN_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_remove_group_admin_req();
    body->set_group_id(gid);
    body->set_target_user_id(target_uid);
    send_request(msg, MessageType::REMOVE_GROUP_ADMIN_RSP, std::move(cb));
}

void ChatClient::approve_join_group(uint64_t gid, uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::APPROVE_JOIN_GROUP_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_approve_join_group_req();
    body->set_group_id(gid);
    body->set_target_user_id(target_uid);
    send_request(msg, MessageType::APPROVE_JOIN_GROUP_RSP, std::move(cb));
}

void ChatClient::reject_join_group(uint64_t gid, uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::REJECT_JOIN_GROUP_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_reject_join_group_req();
    body->set_group_id(gid);
    body->set_target_user_id(target_uid);
    send_request(msg, MessageType::REJECT_JOIN_GROUP_RSP, std::move(cb));
}

void ChatClient::remove_group_member(uint64_t gid, uint64_t target_uid, ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::REMOVE_GROUP_MEMBER_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_remove_group_member_req();
    body->set_group_id(gid);
    body->set_target_user_id(target_uid);
    send_request(msg, MessageType::REMOVE_GROUP_MEMBER_RSP, std::move(cb));
}

// --- 聊天模块 ---

void ChatClient::send_private_chat(uint64_t target_uid, const std::string& text,
                                    ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::PRIVATE_CHAT_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_target_id(target_uid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_private_chat_req()->set_payload(text);
    send_request(msg, MessageType::PRIVATE_CHAT_RSP, std::move(cb));
}

void ChatClient::send_group_chat(uint64_t gid, const std::string& text,
                                  ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::GROUP_CHAT_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_group_id(gid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_group_chat_req()->set_payload(text);
    send_request(msg, MessageType::GROUP_CHAT_RSP, std::move(cb));
}

void ChatClient::get_history(uint64_t target_uid, uint64_t gid, int limit,
                              ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::GET_HISTORY_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_target_id(target_uid);
    msg.set_group_id(gid);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_get_history_req();
    body->set_target_user_id(target_uid);
    body->set_group_id(gid);
    body->set_limit(limit);
    send_request(msg, MessageType::GET_HISTORY_RSP, std::move(cb));
}

// --- 文件模块 ---

void ChatClient::upload_file(const std::string& filepath, ResponseCallback cb) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) {
        log("[错误] 无法打开文件: " + filepath, 2);
        return;
    }
    fseek(fp, 0, SEEK_END);
    uint64_t fsize = static_cast<uint64_t>(ftell(fp));
    fseek(fp, 0, SEEK_SET);

    std::string filedata(static_cast<size_t>(fsize), '\0');
    fread(&filedata[0], 1, static_cast<size_t>(fsize), fp);
    fclose(fp);

    std::string filename = filepath;
    auto pos = filepath.find_last_of('/');
    if (pos != std::string::npos) filename = filepath.substr(pos + 1);

    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::FILE_UPLOAD_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    auto* body = msg.mutable_file_upload_req();
    body->set_file_name(filename);
    body->set_file_size(fsize);
    body->set_file_data(filedata);
    body->set_chunk_seq(0);
    body->set_total_chunks(1);

    log("[信息] 上传文件: " + filename + " (" + std::to_string(fsize) + " bytes)");
    send_request(msg, MessageType::FILE_UPLOAD_RSP, std::move(cb));
}

void ChatClient::download_file(uint64_t file_id, const std::string& save_path,
                                ResponseCallback cb) {
    ChatMessage msg;
    msg.set_type(static_cast<uint32_t>(MessageType::FILE_DOWNLOAD_REQ));
    msg.set_token(token_);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(time(nullptr)));
    msg.mutable_file_download_req()->set_file_id(file_id);

    // 包装回调：下载完成后自动保存文件
    auto save_cb = [this, save_path, cb = std::move(cb)](const ChatMessage& resp) {
        if (resp.has_file_download_rsp() && resp.file_download_rsp().success()) {
            auto& r = resp.file_download_rsp();
            std::string out_path = save_path;
            if (out_path.empty()) out_path = r.file_name();
            FILE* fp = fopen(out_path.c_str(), "wb");
            if (fp) {
                fwrite(r.file_data().data(), 1, r.file_data().size(), fp);
                fclose(fp);
                log("[成功] 文件已保存到: " + out_path);
            } else {
                log("[错误] 无法创建文件: " + out_path, 2);
            }
        }
        if (cb) cb(resp);
    };

    send_request(msg, MessageType::FILE_DOWNLOAD_RSP, save_cb);
}

} // namespace chatroom
