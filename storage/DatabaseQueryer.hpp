#pragma once

#include "service/MessageType.hpp"
#include "service/SessionState.hpp"
#include "DatabaseQueryResult.hpp"
#include "StorageManager.hpp"
#include "sender/VerificationSender.hpp"
#include <openssl/sha.h>
#include <memory>
#include <iostream>
#include <utility>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>

namespace chatroom {

static constexpr size_t FILE_CHUNK_SIZE = 64 * 1024;
static constexpr size_t MAX_MESSAGE_LENGTH = 5000;  // 最大消息长度（与客户端一致）

class DatabaseQueryer {
public:
    explicit DatabaseQueryer(std::shared_ptr<StorageManager> storage,
                             std::shared_ptr<VerificationSender> sender = nullptr)
        : storage_(storage), sender_(sender) {}

    QueryResult query(SessionState conn_state, const Message& msg) {// 查询总函数
        if (msg.type == MessageType::HEARTBEAT_REQ) {
            QueryResult r;
            r.success = true;
            return r;
        }
        int tv = static_cast<int>(msg.type);
        if (tv >= 1 && tv < 100)       return query_account(conn_state, msg);
        if (tv >= 100 && tv < 200)     return query_friend(conn_state, msg);
        if (tv >= 200 && tv < 300)     return query_group(conn_state, msg);
        if (tv >= 300 && tv < 400)     return query_chat(conn_state, msg);
        if (tv >= 420 && tv < 440)     return query_file_send(conn_state, msg);
        return fail("未知消息类型");
    }

private:
    static constexpr int CODE_TTL = 300;         // 验证码有效期（秒）
    static constexpr int RESEND_INTERVAL = 60;   // 重发冷却（秒）
    static constexpr int MAX_ATTEMPTS = 5;       // 最大错误尝试次数

    static std::pair<std::string, std::string> split_two(const std::string& s) {// 分割字符
        size_t pos = s.find('\n');
        return (pos == std::string::npos) ? std::make_pair(s, std::string{})
               : std::make_pair(s.substr(0, pos), s.substr(pos + 1));
    }

    static std::vector<std::string> split_fields(const std::string& s, char delim = '\n') {// 按分隔符切分，保留空字段
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == delim) { out.push_back(std::move(cur)); cur.clear(); }
            else cur += c;
        }
        out.push_back(std::move(cur));
        return out;
    }

    static std::string normalize_email(const std::string& e) {// trim + 转小写
        std::string s = e;
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        auto ep = s.find_last_not_of(" \t\r\n");
        s = s.substr(b, ep - b + 1);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    static std::string normalize_phone(const std::string& p) {// 仅保留数字
        std::string out;
        for (char c : p) if (c >= '0' && c <= '9') out += c;
        return out;
    }

    static bool is_valid_email(const std::string& e) {// 简单邮箱格式校验 + 拒绝注入字符
        if (e.empty() || e.size() > 254) return false;
        // 拒绝控制字符/空格/尖括号，防 SMTP 命令/头注入（CRLF、<、> 等）
        for (unsigned char c : e) {
            if (c <= 0x20 || c >= 0x7f || c == '<' || c == '>') return false;
        }
        size_t at = e.find('@');
        if (at == std::string::npos || at == 0 || at + 1 >= e.size()) return false;
        if (e.find('@', at + 1) != std::string::npos) return false;  // 只允许一个 @
        return e.find('.', at + 1) != std::string::npos;
    }

    static bool is_valid_phone(const std::string& p) {// 大陆手机号 1 开头 11 位
        return p.size() == 11 && p[0] == '1';
    }

    // 校验验证码
    bool verify_code_matches(const std::string& scene, const std::string& channel,
                             const std::string& target, const std::string& code,
                             QueryResult& err) {
        if (storage_->incr_verify_attempt(scene, channel, target, CODE_TTL) > MAX_ATTEMPTS) {
            err = fail("验证码错误次数过多，请重新获取");
            return false;
        }
        std::string saved = storage_->load_verify_code(scene, channel, target);
        if (saved.empty()) { err = fail("验证码已过期或未发送"); return false; }
        if (saved != code) { err = fail("验证码错误"); return false; }
        storage_->del_verify_code(scene, channel, target);
        return true;
    }

    static QueryResult fail(const std::string& msg) {// 快速返回一个失败的查询结果
        QueryResult r; r.success = false; r.error_message = msg; return r;
    }

    static uint64_t extract_target(const Message& msg) {// 提取目标用户id
        return msg.target_id != 0 ? msg.target_id : std::stoull(msg.payload);
    }

    static std::pair<uint64_t, uint64_t> extract_group_target(const Message& msg) {// 提取群组id，和目标用户id
        uint64_t gid = msg.group_id, tid = msg.target_id;
        if (gid == 0 || tid == 0) {
            auto parts = split_two(msg.payload);
            if (gid == 0) gid = std::stoull(parts.first);
            if (tid == 0) tid = std::stoull(parts.second);
        }
        return {gid, tid};
    }

    bool check_guards(SessionState st, QueryResult& r) {// 检查当前状态
        if (st != SessionState::LOGGED_IN) { r = fail("请先登录"); return false; }
        if (!storage_) { r = fail("存储服务未就绪"); return false; }
        return true;
    }

    using StorageBoolOp = bool (StorageManager::*)(uint64_t, uint64_t);
    QueryResult do_friend_op(const Message& msg, StorageBoolOp op, const std::string& err) {// 模板函数
        QueryResult r;
        uint64_t tid = extract_target(msg);
        r.success = (storage_.get()->*op)(msg.sender_id, tid);
        if (!r.success) r.error_message = err;
        return r;
    }

    QueryResult query_account(SessionState conn_state, const Message& msg) {// 处理登录相关的逻辑
        switch (msg.type) {
            case MessageType::LOGIN_REQ:          return handle_login_query(conn_state, msg);
            case MessageType::REGISTER_REQ:       return handle_register_query(conn_state, msg);
            case MessageType::LOGOUT_REQ:         return handle_logout_query(conn_state, msg);
            case MessageType::VERIFY_CODE_REQ:    return handle_verify_code_query(conn_state, msg);
            case MessageType::PASSWORD_RESET_REQ: return handle_password_reset_query(conn_state, msg);
            case MessageType::DELETE_ACCOUNT_REQ: return handle_delete_account_query(conn_state, msg);
            case MessageType::UPDATE_PROFILE_REQ: return handle_update_profile_query(conn_state, msg);
            default: return fail("未知账号操作");
        }
    }

    QueryResult handle_login_query(SessionState conn_state, const Message& msg) {// 登录查询
        if (conn_state == SessionState::LOGGED_IN) return fail("您已登录，请勿重复登录");
        if (!storage_) return fail("存储服务未就绪");

        auto [username, password] = split_two(msg.payload);
        if (!storage_->verify_password(username, password)) return fail("用户名或密码错误");

        auto ur = storage_->get_user_by_username(username);
        if (!ur.success) return ur;
        if (ur.user_status == 2) return fail("账号已被封禁，无法登录");
        if (ur.user_status == 0) return fail("账号未激活，无法登录");

        ur.token = storage_->create_session(ur.user_id, username);
        storage_->set_online(ur.user_id);
        ur.friend_list = storage_->get_friends(ur.user_id);
        ur.offline_messages = storage_->get_offline_messages(ur.user_id);
        return ur;
    }

    QueryResult handle_register_query(SessionState, const Message& msg) {// 注册查询
        QueryResult r;
        if (!storage_) return fail("存储服务未就绪");

        auto parts = split_fields(msg.payload);
        if (parts.size() < 3) return fail("注册参数不完整");
        std::string username = parts[0];
        std::string password = parts[1];
        std::string nickname = parts[2];
        std::string email     = parts.size() > 3 ? normalize_email(parts[3]) : "";
        std::string phone     = parts.size() > 4 ? normalize_phone(parts[4]) : "";
        std::string verify_code = parts.size() > 5 ? parts[5] : "";
        if (nickname.empty()) nickname = username;

        if (storage_->user_exists(username)) return fail("用户名已存在");

        bool has_email = !email.empty();
        bool has_phone = !phone.empty();
        if (has_email || has_phone) {
            if (has_email && !is_valid_email(email)) return fail("邮箱格式不正确");
            if (has_phone && !is_valid_phone(phone)) return fail("手机号格式不正确");
            if (has_email && storage_->email_exists(email)) return fail("该邮箱已被注册");
            if (has_phone && storage_->phone_exists(phone)) return fail("该手机号已被注册");
            if (verify_code.empty()) return fail("请填写验证码");
            std::string channel = has_email ? "email" : "phone";
            std::string target  = has_email ? email : phone;
            QueryResult err;
            if (!verify_code_matches("register", channel, target, verify_code, err)) return err;
        }

        r = storage_->create_user(username, password, nickname, email, phone);
        if (r.success) r.token = storage_->create_session(r.user_id, username);
        return r;
    }

    QueryResult handle_logout_query(SessionState, const Message& msg) {// 登出查询
        if (!storage_) return fail("存储服务未就绪");
        QueryResult r;
        r.success = true;
        if (msg.sender_id != 0) {
            r.friend_list = storage_->get_friends(msg.sender_id);
            storage_->clear_session(msg.sender_id);
            storage_->set_offline(msg.sender_id);
        }
        return r;
    }

    QueryResult handle_verify_code_query(SessionState, const Message& msg) {// 发送验证码
        if (!storage_) return fail("存储服务未就绪");
        if (!sender_) return fail("验证码服务未配置");

        auto parts = split_fields(msg.payload);  // channel \n target \n scene
        if (parts.size() < 3) return fail("参数不完整");
        std::string channel = parts[0];
        std::string target  = parts[1];
        std::string scene   = parts[2];

        bool email = (channel == "email");
        bool phone = (channel == "phone");
        if (!email && !phone) return fail("不支持的发送渠道");
        if (!sender_->supports_channel(channel))
            return fail(email ? "邮件服务未配置" : "手机号验证暂未接入");

        if (email) {
            target = normalize_email(target);
            if (!is_valid_email(target)) return fail("邮箱格式不正确");
        } else {
            target = normalize_phone(target);
            if (!is_valid_phone(target)) return fail("手机号格式不正确");
        }

        if (scene != "register" && scene != "reset") return fail("未知验证码用途");

        if (scene == "register") {
            bool exists = email ? storage_->email_exists(target) : storage_->phone_exists(target);
            if (exists) return fail(email ? "该邮箱已被注册" : "该手机号已被注册");
        } else {
            QueryResult u = email ? storage_->get_user_by_email(target)
                                  : storage_->get_user_by_phone(target);
            if (!u.success) return fail("该账号未注册，无法找回密码");
        }

        // 重发冷却
        if (!storage_->try_verify_rate_limit(scene, channel, target, RESEND_INTERVAL))
            return fail("发送过于频繁，请稍后再试");

        std::string code = generate_verify_code();
        if (!storage_->save_verify_code(scene, channel, target, code, CODE_TTL))
            return fail("验证码存储失败");

        if (!sender_->send(channel, target, code))
            return fail("验证码发送失败，请稍后再试");

        QueryResult r; r.success = true;
        r.expire_seconds = CODE_TTL;
        r.resend_seconds = RESEND_INTERVAL;
        if (phone && sender_->debug_code_enabled()) r.debug_code = code;  // 仅调试模式回显
        return r;
    }

    QueryResult handle_password_reset_query(SessionState, const Message& msg) {// 重置密码
        if (!storage_) return fail("存储服务未就绪");

        auto parts = split_fields(msg.payload); 
        if (parts.size() < 4) return fail("参数不完整");
        std::string channel      = parts[0];
        std::string target       = parts[1];
        std::string new_password = parts[2];
        std::string verify_code  = parts[3];

        bool email = (channel == "email");
        bool phone = (channel == "phone");
        if (!email && !phone) return fail("不支持的渠道");

        if (email) {
            target = normalize_email(target);
            if (!is_valid_email(target)) return fail("邮箱格式不正确");
        } else {
            target = normalize_phone(target);
            if (!is_valid_phone(target)) return fail("手机号格式不正确");
        }
        if (new_password.empty()) return fail("新密码不能为空");

        QueryResult user = email ? storage_->get_user_by_email(target)
                                 : storage_->get_user_by_phone(target);
        if (!user.success) return fail("该账号未注册");

        QueryResult err;
        if (!verify_code_matches("reset", channel, target, verify_code, err)) return err;

        QueryResult r;
        r.success = storage_->update_password(user.user_id, new_password);
        if (!r.success) {
            r.error_message = "重置密码失败";
        } else {
            storage_->clear_session(user.user_id);  // 重置后强制下线
            storage_->set_offline(user.user_id);
        }
        return r;
    }

    QueryResult handle_delete_account_query(SessionState, const Message& msg) {// 删除用户
        if (!storage_) return fail("存储服务未就绪");
        auto user = storage_->get_user_by_id(msg.sender_id);
        if (!user.success) return fail("用户不存在");
        if (!storage_->verify_password(user.username, msg.payload)) return fail("密码错误，无法注销账号");
        QueryResult r;
        r.success = storage_->delete_user(msg.sender_id);
        if (!r.success) r.error_message = "注销失败";
        return r;
    }

    QueryResult handle_update_profile_query(SessionState, const Message& msg) {// 更新昵称
        if (!storage_) return fail("存储服务未就绪");
        std::string nickname = msg.payload;
        if (nickname.empty()) return fail("昵称不能为空");
        if (nickname.size() > 128) return fail("昵称过长（最多 128 字符）");
        QueryResult r;
        r.success = storage_->update_nickname(msg.sender_id, nickname);
        if (!r.success) r.error_message = "修改昵称失败";
        else r.nickname = nickname;
        return r;
    }

    QueryResult query_friend(SessionState conn_state, const Message& msg) {// 好友查询
        QueryResult r;
        if (conn_state != SessionState::LOGGED_IN) return fail("请先登录");
        if (!storage_) return fail("存储服务未就绪");

        switch (msg.type) {
            case MessageType::ADD_FRIEND_REQ:     return handle_add_friend_query(msg);
            case MessageType::DELETE_FRIEND_REQ:  return do_friend_op(msg, &StorageManager::remove_friend, "删除好友失败");
            case MessageType::QUERY_FRIEND_REQ:   { r.success = true; r.friend_list = storage_->get_friends(msg.sender_id); return r; }
            case MessageType::QUERY_BLOCKED_REQ: { r.success = true; r.friend_list = storage_->get_blocked_users(msg.sender_id); return r; }
            case MessageType::BLOCK_FRIEND_REQ: {
                uint64_t tid = extract_target(msg);
                if (!storage_->is_friend(msg.sender_id, tid)) return fail("不是好友，无法拉黑");
                return do_friend_op(msg, &StorageManager::block_friend, "屏蔽失败");
            }
            case MessageType::UNBLOCK_FRIEND_REQ: {
                uint64_t tid = extract_target(msg);
                if (!storage_->is_friend(msg.sender_id, tid)) return fail("不是好友，无法解除拉黑");
                return do_friend_op(msg, &StorageManager::unblock_friend, "解除屏蔽失败");
            }
            default: return fail("未知好友操作");
        }
    }

    QueryResult handle_add_friend_query(const Message& msg) {// 添加好友（支持用户ID或邮箱）
        QueryResult r;
        uint64_t tid = msg.target_id;  // 信封 target_id 优先
        if (tid == 0) {
            auto parts = split_two(msg.payload);  // 用户ID \n 邮箱
            try {
                if (!parts.first.empty()) tid = std::stoull(parts.first);
            } catch (...) { tid = 0; }  // 非数字或 "0" 都视为未提供ID
            if (tid == 0) {
                std::string email = normalize_email(parts.second);
                if (!is_valid_email(email)) return fail("邮箱格式不正确");
                QueryResult u = storage_->get_user_by_email(email);
                if (!u.success) return fail("该邮箱未注册，无法添加");
                tid = u.user_id;
            }
        }
        if (tid == msg.sender_id) return fail("不能添加自己为好友");
        if (!storage_->get_user_by_id(tid).success) return fail("目标用户不存在");
        if (storage_->is_friend(msg.sender_id, tid)) return fail("已经是好友了");
        r.success = storage_->add_friend(msg.sender_id, tid);
        if (!r.success) r.error_message = "添加好友失败";
        return r;
    }

    QueryResult query_group(SessionState conn_state, const Message& msg) {// 群组查询
        QueryResult r;
        if (!check_guards(conn_state, r)) return r;

        switch (msg.type) {
            case MessageType::CREATE_GROUP_REQ:          return handle_create_group_query(msg);
            case MessageType::DISMISS_GROUP_REQ:         return handle_dismiss_group_query(msg);
            case MessageType::JOIN_GROUP_REQ:            return handle_join_group_query(msg);
            case MessageType::QUIT_GROUP_REQ:            return handle_quit_group_query(msg);
            case MessageType::QUERY_GROUP_LIST_REQ:      return handle_query_group_list_query(msg);
            case MessageType::QUERY_GROUP_MEMBERS_REQ:   return handle_query_group_members_query(msg);
            case MessageType::ADD_GROUP_ADMIN_REQ:       return handle_add_group_admin_query(msg);
            case MessageType::REMOVE_GROUP_ADMIN_REQ:    return handle_remove_group_admin_query(msg);
            case MessageType::APPROVE_JOIN_GROUP_REQ:    return handle_approve_join_group_query(msg);
            case MessageType::REJECT_JOIN_GROUP_REQ:    return handle_reject_join_group_query(msg);
            case MessageType::REMOVE_GROUP_MEMBER_REQ:   return handle_remove_group_member_query(msg);
            default: return fail("未知群组操作");
        }
    }

    QueryResult handle_create_group_query(const Message& msg) {
        QueryResult r;
        auto [pub_str, rest] = split_two(msg.payload);
        auto [name, desc] = split_two(rest);
        bool is_public = (pub_str != "0");
        uint64_t gid = storage_->create_group(name, desc, msg.sender_id, is_public);
        r.success = (gid != 0);
        if (r.success) r.group_id = gid; else r.error_message = "创建群组失败";
        return r;
    }

    QueryResult handle_dismiss_group_query(const Message& msg) {// 解散群组
        QueryResult r;
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        r.group_id = gid;
        r.group_members = storage_->get_group_members(gid);
        r.success = storage_->dismiss_group(gid, msg.sender_id);
        if (!r.success) {
            r.error_message = "解散群组失败";
            r.group_members.clear();
        }
        return r;
    }

    QueryResult handle_join_group_query(const Message& msg) {// 处理加入群组
        QueryResult r;
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        if (storage_->is_group_member(gid, msg.sender_id)) return fail("已经是群组成员");

        if (storage_->is_group_public(gid)) {
            // 公开群组：直接加入
            r.success = storage_->join_group(gid, msg.sender_id);
            if (r.success) { r.group_id = gid; r.group_members = storage_->get_group_members(gid); }
            else r.error_message = "加入群组失败";
        } else {
            // 私密群组：发送加入请求
            if (storage_->is_join_pending(gid, msg.sender_id))
                return fail("已发送过加入请求，请等待管理员审核");
            r.success = storage_->request_join_group(gid, msg.sender_id);
            if (r.success) {
                r.group_id = gid;
                r.pending = true;
                r.group_members = storage_->get_group_members(gid);
            } else {
                r.error_message = "发送加入请求失败";
            }
        }
        return r;
    }

    QueryResult handle_quit_group_query(const Message& msg) {// 退出群组
        QueryResult r;
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        r.success = storage_->quit_group(gid, msg.sender_id);
        if (r.success) {
            r.group_id = gid;
            r.group_members = storage_->get_group_members(gid);
        }
        else r.error_message = "退出群组失败";
        return r;
    }

    QueryResult handle_query_group_list_query(const Message& msg) {// 获取用户加入的群组列表
        QueryResult r; r.success = true;
        r.group_list = storage_->get_user_groups(msg.sender_id);
        return r;
    }

    QueryResult handle_query_group_members_query(const Message& msg) {// 查询群组成员列表
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        if (!storage_->is_group_member(gid, msg.sender_id)) return fail("你不是该群组成员");
        QueryResult r; r.success = true;
        r.group_members = storage_->get_group_members(gid);
        return r;
    }

    using GroupAdminOp = bool (StorageManager::*)(uint64_t, uint64_t, uint64_t);
    QueryResult do_group_admin_op(const Message& msg, GroupAdminOp op, const std::string& err) {// 群组查询模板函数
        QueryResult r;
        auto [gid, tid] = extract_group_target(msg);
        r.success = (storage_.get()->*op)(gid, msg.sender_id, tid);
        if (r.success) {
            r.group_id = gid;
            r.group_members = storage_->get_group_members(gid);
        }
        else r.error_message = err;
        return r;
    }

    QueryResult handle_add_group_admin_query(const Message& msg) {
        return do_group_admin_op(msg, &StorageManager::add_admin, "添加管理员失败");
    }
    QueryResult handle_remove_group_admin_query(const Message& msg) {
        return do_group_admin_op(msg, &StorageManager::remove_admin, "移除管理员失败");
    }
    QueryResult handle_approve_join_group_query(const Message& msg) {
        return do_group_admin_op(msg, &StorageManager::approve_join, "批准加入失败");
    }
    QueryResult handle_reject_join_group_query(const Message& msg) {
        return do_group_admin_op(msg, &StorageManager::reject_join, "拒绝加入请求失败");
    }
    QueryResult handle_remove_group_member_query(const Message& msg) {
        return do_group_admin_op(msg, &StorageManager::remove_member, "移除群组成员失败");
    }

    QueryResult query_chat(SessionState conn_state, const Message& msg) {// 聊天查询
        QueryResult r;
        if (!check_guards(conn_state, r)) return r;

        switch (msg.type) {
            case MessageType::PRIVATE_CHAT_REQ:  return handle_private_chat_query(msg);
            case MessageType::GROUP_CHAT_REQ:    return handle_group_chat_query(msg);
            case MessageType::GET_HISTORY_REQ:   return handle_get_history_query(msg);
            default: return fail("未知聊天操作");
        }
    }

    QueryResult handle_private_chat_query(const Message& msg) {// 处理私聊
        if (msg.payload.size() > MAX_MESSAGE_LENGTH)
            return fail("消息过长（最多 5000 字），无法发送");
        if (!storage_->is_friend(msg.sender_id, msg.target_id)) return fail("不是好友，无法发送私聊");
        if (storage_->is_blocked_by(msg.target_id, msg.sender_id)) return fail("你已被对方拉黑，无法发送消息");

        storage_->save_message(msg.sender_id, msg.target_id, msg.payload,
                               static_cast<int>(MessageType::PRIVATE_CHAT_REQ));

        if (!storage_->is_online(msg.target_id)) {
            auto sender = storage_->get_user_by_id(msg.sender_id);
            std::string name = sender.success ? sender.username : std::to_string(msg.sender_id);
            storage_->save_offline_message(msg.target_id, msg.sender_id, name, msg.payload);
        }
        return {true, "", false, 0, "", "", false};
    }

    QueryResult handle_group_chat_query(const Message& msg) {// 处理群组聊天
        if (msg.payload.size() > MAX_MESSAGE_LENGTH)
            return fail("消息过长（最多 5000 字），无法发送");
        if (!storage_->is_group_member(msg.group_id, msg.sender_id)) return fail("你不在该群组中");
        storage_->save_group_message(msg.group_id, msg.sender_id, msg.payload);
        QueryResult r; r.success = true;
        r.group_members = storage_->get_group_members(msg.group_id);
        return r;
    }

    QueryResult handle_get_history_query(const Message& msg) {// 处理历史记录查询
        auto [target_str, rest] = split_two(msg.payload);
        auto [group_str, limit_str] = split_two(rest);
        uint64_t tid = target_str.empty() ? 0 : std::stoull(target_str);
        uint64_t gid = group_str.empty() ? 0 : std::stoull(group_str);
        int limit = limit_str.empty() ? 50 : std::stoi(limit_str);

        if (gid == 0 && tid == 0) return fail("缺少查询参数");
        if (gid != 0 && !storage_->is_group_member(gid, msg.sender_id))
            return fail("你不是该群组成员，无法查看聊天记录");
        QueryResult r; r.success = true;
        r.history = (gid != 0) ? storage_->get_group_history(gid, limit)
                              : storage_->get_history(msg.sender_id, tid, limit);
        return r;
    }

    QueryResult query_file_send(SessionState conn_state, const Message& msg) {// 文件查询
        QueryResult r;
        if (!check_guards(conn_state, r)) return r;

        switch (msg.type) {
            case MessageType::FILE_SEND_REQ:             return handle_file_send_query(msg);
            case MessageType::FILE_SEND_CHUNK_REQ:       return handle_file_send_chunk_query(msg);
            case MessageType::FILE_TRANSFER_ACCEPT_REQ:  return handle_file_transfer_accept_query(msg);
            case MessageType::FILE_RECEIVE_CHUNK_REQ:    return handle_file_receive_chunk_query(msg);
             case MessageType::FILE_TRANSFER_STATUS_REQ:  return handle_file_transfer_status_query(msg);
             case MessageType::FILE_FINALIZE_REQ:        return handle_file_finalize_query(msg);
             default: return fail("未知文件发送操作");
        }
    }

    QueryResult handle_file_send_query(const Message& msg) {// 文件发送
        if (!storage_->is_friend(msg.sender_id, msg.target_id))
            return fail("不是好友，无法发送文件");
        if (storage_->is_blocked_by(msg.target_id, msg.sender_id))
            return fail("你已被对方拉黑");

        // 小文件：直接通过 msg.file_data + msg.file_size 获取
        // 大文件：msg.total_chunks 已由客户端计算
        uint32_t total = msg.total_chunks > 0 ? msg.total_chunks : 1;
        uint64_t tid = storage_->create_transfer(msg.sender_id, msg.target_id,
                                                  msg.payload, msg.file_size, total, msg.file_hash);
        if (tid == 0) return fail("创建传输记录失败");

        // 小文件
        if (total <= 1 && !msg.file_data.empty()) {
            if (!msg.chunk_hash.empty()) {// 哈希校验
                unsigned char computed[SHA256_DIGEST_LENGTH];
                SHA256(reinterpret_cast<const unsigned char*>(msg.file_data.data()),
                       msg.file_data.size(), computed);// 计算哈希
                std::string computed_hex;
                computed_hex.reserve(SHA256_DIGEST_LENGTH * 2);
                for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                    char buf[3];
                    snprintf(buf, sizeof(buf), "%02x", computed[i]);
                    computed_hex += buf;
                }// 保存哈希
                if (computed_hex != msg.chunk_hash) {// 哈希校验
                    storage_->reject_transfer(tid);
                    return fail("分片哈希校验失败");
                }
            }
            storage_->save_transfer_chunk_data(tid, 0, msg.file_data, msg.chunk_hash);
            storage_->record_sender_chunk(tid, 0);
            storage_->complete_transfer(tid);
        }

        storage_->add_pending_transfer(msg.target_id, tid);

        QueryResult r; r.success = true; r.transfer_id = tid; return r;
    }

    QueryResult handle_file_send_chunk_query(const Message& msg) {// 大文件传输处理
        uint64_t tid = 0;
        try { tid = std::stoull(msg.payload); } catch (...) { return fail("无效的传输ID"); }

        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");
        if (info.sender_id != msg.sender_id) return fail("无权操作此传输");

        if (!msg.chunk_hash.empty()) {
            unsigned char computed[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char*>(msg.file_data.data()),
                   msg.file_data.size(), computed);
            char hex_buf[SHA256_DIGEST_LENGTH * 2 + 1];
            for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
                snprintf(hex_buf + i * 2, 3, "%02x", computed[i]);
            if (msg.chunk_hash != std::string(hex_buf, SHA256_DIGEST_LENGTH * 2))
                return fail("分片哈希校验失败");
        }

        if (!storage_->save_transfer_chunk_data(tid, msg.chunk_seq, msg.file_data, msg.chunk_hash))
            return fail("保存分片失败");
        storage_->record_sender_chunk(tid, msg.chunk_seq);

        QueryResult r; r.success = true; r.transfer_id = tid;
        r.target_user_id = info.receiver_id;
        if (msg.chunk_seq == msg.total_chunks - 1 || msg.total_chunks <= 1) {
            storage_->complete_transfer(tid);
        }
        return r;
    }

    QueryResult handle_file_transfer_accept_query(const Message& msg) {// 文件发送
        auto [tid_str, flag_str] = split_two(msg.payload);
        uint64_t tid = std::stoull(tid_str);
        bool accept = (flag_str == "1");
        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");
        if (info.receiver_id != msg.sender_id) return fail("无权操作此传输");

        if (accept) {
            QueryResult r; r.success = true; r.transfer_id = tid;
            r.file_name = info.file_name; r.file_size = info.file_size;
            r.total_chunks = info.total_chunks;
            return r;
        } else {
            storage_->reject_transfer(tid);
            return {true, "", false, 0, "", "", false};
        }
    }

    QueryResult handle_file_receive_chunk_query(const Message& msg) {// 文件接收
        uint64_t tid = 0;
        try { tid = std::stoull(msg.payload); } catch (...) { return fail("无效的传输ID"); }

        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");
        if (info.receiver_id != msg.sender_id) return fail("无权接收此文件");

        std::string chunk_data = storage_->get_transfer_chunk_data(tid, msg.chunk_seq);
        if (chunk_data.empty()) return fail("分片数据不存在");

        // 获取该分片的哈希值
        std::string chunk_hash = storage_->get_transfer_chunk_hash(tid, msg.chunk_seq);

        storage_->record_receiver_chunk(tid, msg.chunk_seq);

        QueryResult r; r.success = true; r.transfer_id = tid;
        r.file_data = std::move(chunk_data);
        r.chunk_hash = std::move(chunk_hash);
        r.chunk_seq = msg.chunk_seq;
        r.total_chunks = info.total_chunks;
        r.file_name = info.file_name;
        r.file_size = info.file_size;
        return r;
    }

    QueryResult handle_file_transfer_status_query(const Message& msg) {// 文件传输状态
        uint64_t tid = 0;
        try { tid = std::stoull(msg.payload); } catch (...) { return fail("无效的传输ID"); }

        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");

        QueryResult r; r.success = true; r.transfer_id = tid;
        r.file_name = info.file_name; r.file_size = info.file_size;
        r.total_chunks = info.total_chunks;
        r.file_hash = info.file_hash;
        r.transfer_status = info.status;
        r.sender_received_chunks = storage_->get_sender_chunks(tid);
        r.receiver_received_chunks = storage_->get_receiver_chunks(tid);
        return r;
    }

    QueryResult handle_file_finalize_query(const Message& msg) {// 拼接文件分片
        auto [tid_str, hash_str] = split_two(msg.payload);
        uint64_t tid = 0;
        try { tid = std::stoull(tid_str); } catch (...) { return fail("无效的传输ID"); }

        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");

        std::string role = (msg.sender_id == info.sender_id) ? "sender" : "receiver";
        if (msg.sender_id != info.sender_id && msg.sender_id != info.receiver_id)
            return fail("无权操作此传输");

        std::string error_msg;
        std::string final_path = storage_->assemble_final_file(tid, info.file_name,
                                                                info.file_hash, role, error_msg);
        if (final_path.empty()) {
            return fail("组装文件失败: " + error_msg);
        }

        storage_->complete_transfer(tid);

        QueryResult r; 
        r.success = true; r.transfer_id = tid;
        r.final_path = final_path;
        r.target_user_id = info.receiver_id;
        r.file_name = info.file_name;
        return r;
    }

    std::shared_ptr<StorageManager> storage_;
    std::shared_ptr<VerificationSender> sender_;
};

} // namespace chatroom