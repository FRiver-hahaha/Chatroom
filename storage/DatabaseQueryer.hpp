#pragma once

#include "service/MessageType.hpp"
#include "service/SessionState.hpp"
#include "DatabaseQueryResult.hpp"
#include "StorageManager.hpp"
#include <openssl/sha.h>
#include <memory>
#include <iostream>
#include <utility>
#include <sys/stat.h>

namespace chatroom {

// 分片大小：64KB
static constexpr size_t FILE_CHUNK_SIZE = 64 * 1024;

class DatabaseQueryer {
public:
    explicit DatabaseQueryer(std::shared_ptr<StorageManager> storage)
        : storage_(storage) {}

    QueryResult query(SessionState conn_state, const Message& msg) {
        int tv = static_cast<int>(msg.type);
        if (tv >= 1 && tv < 100)       return query_account(conn_state, msg);
        if (tv >= 100 && tv < 200)     return query_friend(conn_state, msg);
        if (tv >= 200 && tv < 300)     return query_group(conn_state, msg);
        if (tv >= 300 && tv < 400)     return query_chat(conn_state, msg);
        if (tv >= 420 && tv < 440)     return query_file_send(conn_state, msg);
        return fail("未知消息类型");
    }

private:
    static std::pair<std::string, std::string> split_two(const std::string& s) {
        size_t pos = s.find('\n');
        return (pos == std::string::npos) ? std::make_pair(s, std::string{})
               : std::make_pair(s.substr(0, pos), s.substr(pos + 1));
    }

    static QueryResult fail(const std::string& msg) {
        QueryResult r; r.success = false; r.error_message = msg; return r;
    }

    // helper: extract target_id from message
    static uint64_t extract_target(const Message& msg) {
        return msg.target_id != 0 ? msg.target_id : std::stoull(msg.payload);
    }

    // helper: extract group_id and target_id from admin-style message
    static std::pair<uint64_t, uint64_t> extract_group_target(const Message& msg) {
        uint64_t gid = msg.group_id, tid = msg.target_id;
        if (gid == 0 || tid == 0) {
            auto parts = split_two(msg.payload);
            if (gid == 0) gid = std::stoull(parts.first);
            if (tid == 0) tid = std::stoull(parts.second);
        }
        return {gid, tid};
    }

    // check login + storage guards, returns false if blocked
    bool check_guards(SessionState st, QueryResult& r) {
        if (st != SessionState::LOGGED_IN) { r = fail("请先登录"); return false; }
        if (!storage_) { r = fail("存储服务未就绪"); return false; }
        return true;
    }

    // simple storage op that just needs target_id
    using StorageBoolOp = bool (StorageManager::*)(uint64_t, uint64_t);
    QueryResult do_friend_op(const Message& msg, StorageBoolOp op, const std::string& err) {
        QueryResult r;
        uint64_t tid = extract_target(msg);
        r.success = (storage_.get()->*op)(msg.sender_id, tid);
        if (!r.success) r.error_message = err;
        return r;
    }

    // ===== Account =====
    QueryResult query_account(SessionState conn_state, const Message& msg) {
        switch (msg.type) {
            case MessageType::LOGIN_REQ:          return handle_login_query(conn_state, msg);
            case MessageType::REGISTER_REQ:       return handle_register_query(conn_state, msg);
            case MessageType::LOGOUT_REQ:         return handle_logout_query(conn_state, msg);
            case MessageType::VERIFY_CODE_REQ:    return handle_verify_code_query(conn_state, msg);
            case MessageType::PASSWORD_RESET_REQ: return handle_password_reset_query(conn_state, msg);
            case MessageType::DELETE_ACCOUNT_REQ: return handle_delete_account_query(conn_state, msg);
            default: return fail("未知账号操作");
        }
    }

    QueryResult handle_login_query(SessionState conn_state, const Message& msg) {
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

    QueryResult handle_register_query(SessionState, const Message& msg) {
        QueryResult r;
        if (!storage_) return fail("存储服务未就绪");
        auto [username, password_nick] = split_two(msg.payload);
        auto [password, nickname] = split_two(password_nick);
        if (nickname.empty()) nickname = username;

        if (storage_->user_exists(username)) return fail("用户名已存在");

        r = storage_->create_user(username, password, nickname);
        if (r.success) r.token = storage_->create_session(r.user_id, username);
        return r;
    }

    QueryResult handle_logout_query(SessionState, const Message& msg) {
        if (!storage_) return fail("存储服务未就绪");
        if (msg.sender_id != 0) {
            storage_->clear_session(msg.sender_id);
            storage_->set_offline(msg.sender_id);
        }
        return {true, "", 0, "", "", false};
    }

    QueryResult handle_verify_code_query(SessionState, const Message&) {
        QueryResult r; r.success = true; r.verify_code = "123456"; return r;
    }

    QueryResult handle_password_reset_query(SessionState, const Message& msg) {
        if (!storage_) return fail("存储服务未就绪");
        auto [uid_str, new_pass] = split_two(msg.payload);
        uint64_t uid = std::stoull(uid_str);
        QueryResult r;
        r.success = storage_->update_password(uid, new_pass);
        if (!r.success) r.error_message = "重置密码失败";
        return r;
    }

    QueryResult handle_delete_account_query(SessionState, const Message& msg) {
        if (!storage_) return fail("存储服务未就绪");
        auto user = storage_->get_user_by_id(msg.sender_id);
        if (!user.success) return fail("用户不存在");
        if (!storage_->verify_password(user.username, msg.payload)) return fail("密码错误，无法注销账号");
        QueryResult r;
        r.success = storage_->delete_user(msg.sender_id);
        if (!r.success) r.error_message = "注销失败";
        return r;
    }

    // ===== Friend =====
    QueryResult query_friend(SessionState conn_state, const Message& msg) {
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

    QueryResult handle_add_friend_query(const Message& msg) {
        QueryResult r;
        uint64_t tid = extract_target(msg);
        if (!storage_->get_user_by_id(tid).success) return fail("目标用户不存在");
        if (storage_->is_friend(msg.sender_id, tid)) return fail("已经是好友了");
        r.success = storage_->add_friend(msg.sender_id, tid);
        if (!r.success) r.error_message = "添加好友失败";
        return r;
    }

    // ===== Group =====
    QueryResult query_group(SessionState conn_state, const Message& msg) {
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
        // payload format: "is_public\nname\ndescription"
        auto [pub_str, rest] = split_two(msg.payload);
        auto [name, desc] = split_two(rest);
        bool is_public = (pub_str != "0");
        uint64_t gid = storage_->create_group(name, desc, msg.sender_id, is_public);
        r.success = (gid != 0);
        if (r.success) r.group_id = gid; else r.error_message = "创建群组失败";
        return r;
    }

    QueryResult handle_dismiss_group_query(const Message& msg) {
        QueryResult r;
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        r.success = storage_->dismiss_group(gid, msg.sender_id);
        if (!r.success) r.error_message = "解散群组失败";
        return r;
    }

    QueryResult handle_join_group_query(const Message& msg) {
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
                r.group_members = storage_->get_group_members(gid);
            } else {
                r.error_message = "发送加入请求失败";
            }
        }
        return r;
    }

    QueryResult handle_quit_group_query(const Message& msg) {
        QueryResult r;
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        r.success = storage_->quit_group(gid, msg.sender_id);
        if (!r.success) r.error_message = "退出群组失败";
        return r;
    }

    QueryResult handle_query_group_list_query(const Message& msg) {
        QueryResult r; r.success = true;
        r.group_list = storage_->get_user_groups(msg.sender_id);
        return r;
    }

    QueryResult handle_query_group_members_query(const Message& msg) {
        uint64_t gid = msg.group_id; if (gid == 0) gid = std::stoull(msg.payload);
        if (!storage_->is_group_member(gid, msg.sender_id)) return fail("你不是该群组成员");
        QueryResult r; r.success = true;
        r.group_members = storage_->get_group_members(gid);
        return r;
    }

    // Group admin operations share identical structure — extract gid+tid, call storage
    using GroupAdminOp = bool (StorageManager::*)(uint64_t, uint64_t, uint64_t);
    QueryResult do_group_admin_op(const Message& msg, GroupAdminOp op, const std::string& err) {
        QueryResult r;
        auto [gid, tid] = extract_group_target(msg);
        r.success = (storage_.get()->*op)(gid, msg.sender_id, tid);
        if (r.success) r.group_id = gid;
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

    // ===== Chat =====
    QueryResult query_chat(SessionState conn_state, const Message& msg) {
        QueryResult r;
        if (!check_guards(conn_state, r)) return r;

        switch (msg.type) {
            case MessageType::PRIVATE_CHAT_REQ:  return handle_private_chat_query(msg);
            case MessageType::GROUP_CHAT_REQ:    return handle_group_chat_query(msg);
            case MessageType::GET_HISTORY_REQ:   return handle_get_history_query(msg);
            default: return fail("未知聊天操作");
        }
    }

    QueryResult handle_private_chat_query(const Message& msg) {
        if (!storage_->is_friend(msg.sender_id, msg.target_id)) return fail("不是好友，无法发送私聊");
        if (storage_->is_blocked_by(msg.target_id, msg.sender_id)) return fail("你已被对方拉黑，无法发送消息");

        storage_->save_message(msg.sender_id, msg.target_id, msg.payload,
                               static_cast<int>(MessageType::PRIVATE_CHAT_REQ));

        if (!storage_->is_online(msg.target_id)) {
            auto sender = storage_->get_user_by_id(msg.sender_id);
            std::string name = sender.success ? sender.username : std::to_string(msg.sender_id);
            storage_->save_offline_message(msg.target_id, msg.sender_id, name, msg.payload);
        }
        return {true, "", 0, "", "", false};
    }

    QueryResult handle_group_chat_query(const Message& msg) {
        if (!storage_->is_group_member(msg.group_id, msg.sender_id)) return fail("你不在该群组中");
        storage_->save_group_message(msg.group_id, msg.sender_id, msg.payload);
        QueryResult r; r.success = true;
        r.group_members = storage_->get_group_members(msg.group_id);
        return r;
    }

    QueryResult handle_get_history_query(const Message& msg) {
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

    // ===== File Send (420-439) =====
    QueryResult query_file_send(SessionState conn_state, const Message& msg) {
        QueryResult r;
        if (!check_guards(conn_state, r)) return r;

        switch (msg.type) {
            case MessageType::FILE_SEND_REQ:             return handle_file_send_query(msg);
            case MessageType::FILE_SEND_CHUNK_REQ:       return handle_file_send_chunk_query(msg);
            case MessageType::FILE_TRANSFER_ACCEPT_REQ:  return handle_file_transfer_accept_query(msg);
            case MessageType::FILE_RECEIVE_CHUNK_REQ:    return handle_file_receive_chunk_query(msg);
            case MessageType::FILE_TRANSFER_STATUS_REQ:  return handle_file_transfer_status_query(msg);
            default: return fail("未知文件发送操作");
        }
    }

    QueryResult handle_file_send_query(const Message& msg) {
        // 校验好友关系
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

        // 小文件：直接保存完整数据
        if (total <= 1 && !msg.file_data.empty()) {
            // 验证小文件哈希
            if (!msg.chunk_hash.empty()) {
                unsigned char computed[SHA256_DIGEST_LENGTH];
                SHA256(reinterpret_cast<const unsigned char*>(msg.file_data.data()),
                       msg.file_data.size(), computed);
                std::string computed_hex;
                computed_hex.reserve(SHA256_DIGEST_LENGTH * 2);
                for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                    char buf[3];
                    snprintf(buf, sizeof(buf), "%02x", computed[i]);
                    computed_hex += buf;
                }
                if (computed_hex != msg.chunk_hash) {
                    storage_->reject_transfer(tid);
                    return fail("分片哈希校验失败");
                }
            }
            storage_->save_transfer_chunk_data(tid, 0, msg.file_data, msg.chunk_hash);
            storage_->record_sender_chunk(tid, 0);
            storage_->complete_transfer(tid);
        }

        // 记录为对方的待处理传输
        storage_->add_pending_transfer(msg.target_id, tid);

        QueryResult r; r.success = true; r.transfer_id = tid; return r;
    }

    QueryResult handle_file_send_chunk_query(const Message& msg) {
        // msg.payload = transfer_id (string), msg.chunk_seq, msg.file_data, msg.total_chunks
        uint64_t tid = 0;
        try { tid = std::stoull(msg.payload); } catch (...) { return fail("无效的传输ID"); }

        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");
        if (info.sender_id != msg.sender_id) return fail("无权操作此传输");

        // 验证分片哈希（hex 比较）
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

        // 保存分片数据 + 哈希
        if (!storage_->save_transfer_chunk_data(tid, msg.chunk_seq, msg.file_data, msg.chunk_hash))
            return fail("保存分片失败");
        storage_->record_sender_chunk(tid, msg.chunk_seq);

        // 最后一个分片：标记完成
        QueryResult r; r.success = true; r.transfer_id = tid;
        r.target_user_id = info.receiver_id;  // 让 dispatcher 能查找接收方 fd
        if (msg.chunk_seq == msg.total_chunks - 1 || msg.total_chunks <= 1) {
            storage_->complete_transfer(tid);
        }
        return r;
    }

    QueryResult handle_file_transfer_accept_query(const Message& msg) {
        // msg.payload = "transfer_id\naccept_flag" (1=accept, 0=reject)
        auto [tid_str, flag_str] = split_two(msg.payload);
        uint64_t tid = std::stoull(tid_str);
        bool accept = (flag_str == "1");

        auto info = storage_->get_transfer_info(tid);
        if (info.transfer_id == 0) return fail("传输记录不存在");
        if (info.receiver_id != msg.sender_id) return fail("无权操作此传输");

        if (accept) {
            // B 接受传输：可以开始接收分片
            QueryResult r; r.success = true; r.transfer_id = tid;
            r.file_name = info.file_name; r.file_size = info.file_size;
            r.total_chunks = info.total_chunks;
            return r;
        } else {
            storage_->reject_transfer(tid);
            return {true, "", 0, "", "", false};
        }
    }

    QueryResult handle_file_receive_chunk_query(const Message& msg) {
        // msg.payload = transfer_id, msg.chunk_seq
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

    QueryResult handle_file_transfer_status_query(const Message& msg) {
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

    std::shared_ptr<StorageManager> storage_;
};

} // namespace chatroom