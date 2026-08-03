#pragma once

#include "service/MessageType.hpp"
#include "service/SessionState.hpp"
#include "DatabaseQueryResult.hpp"
#include "StorageManager.hpp"
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
        if (tv >= 400 && tv < 500)     return query_file(conn_state, msg);
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

    // ===== File =====
    QueryResult query_file(SessionState conn_state, const Message& msg) {
        QueryResult r;
        if (!check_guards(conn_state, r)) return r;

        switch (msg.type) {
            case MessageType::FILE_UPLOAD_REQ:        return handle_file_upload_query(msg);
            case MessageType::FILE_DOWNLOAD_REQ:      return handle_file_download_query(msg);
            case MessageType::FILE_UPLOAD_CHUNK_REQ:  return handle_file_chunk_upload_query(msg);
            case MessageType::FILE_UPLOAD_STATUS_REQ: return handle_file_upload_status_query(msg);
            case MessageType::FILE_DOWNLOAD_CHUNK_REQ: return handle_file_download_chunk_query(msg);
            default: return fail("未知文件操作");
        }
    }

    QueryResult handle_file_upload_query(const Message& msg) {
        QueryResult r;
        mkdir("/tmp/chatroom_files/", 0755);
        std::string path = "/tmp/chatroom_files/" + msg.payload;
        if (msg.chunk_seq == 0) {
            FILE* fp = fopen(path.c_str(), "wb");
            if (!fp) return fail("无法创建文件");
            fwrite(msg.file_data.data(), 1, msg.file_data.size(), fp);
            fclose(fp);
        }
        uint64_t fid = storage_->save_file_metadata(msg.payload, msg.file_size, path,
                                                     msg.sender_id, msg.target_id);
        r.success = (fid != 0);
        if (r.success) { r.file_id = fid; r.file_name = msg.payload; r.file_size = msg.file_size; }
        else r.error_message = "文件上传失败";
        return r;
    }

    QueryResult handle_file_chunk_upload_query(const Message& msg) {
        QueryResult r;
        mkdir("/tmp/chatroom_files/", 0755);
        std::string path = "/tmp/chatroom_files/" + msg.payload;

        // 写入分片数据
        FILE* fp = fopen(path.c_str(), msg.chunk_seq == 0 ? "wb" : "ab");
        if (!fp) return fail("无法写入文件");
        fwrite(msg.file_data.data(), 1, msg.file_data.size(), fp);
        fclose(fp);

        // 记录分片到 Redis
        storage_->record_file_chunk(msg.sender_id, msg.payload, msg.file_size, msg.chunk_seq);

        if (msg.chunk_seq == msg.total_chunks - 1 || msg.total_chunks <= 1) {
            // 最后一个分片：保存元数据，清理 Redis 状态
            uint64_t fid = storage_->save_file_metadata(msg.payload, msg.file_size, path,
                                                         msg.sender_id, msg.target_id);
            storage_->clear_file_chunks(msg.sender_id, msg.payload, msg.file_size);
            r.success = (fid != 0);
            if (r.success) { r.file_id = fid; r.file_name = msg.payload; r.file_size = msg.file_size; }
            else r.error_message = "文件保存失败";
        } else {
            r.success = true;
        }
        return r;
    }

    QueryResult handle_file_download_query(const Message& msg) {
        QueryResult r;
        uint64_t fid = msg.target_id; if (fid == 0) fid = std::stoull(msg.payload);
        auto info = storage_->get_file_info(fid);
        r.success = (info.file_id != 0);
        if (r.success) {
            r.file_id   = info.file_id;
            r.file_name = info.file_name;
            r.file_size = info.file_size;
            // 小文件直接返回内容，大文件仅返回元数据（客户端走分片下载）
            if (info.file_size <= FILE_CHUNK_SIZE) {
                FILE* fp = fopen(info.file_path.c_str(), "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END);
                    long sz = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    r.file_data.resize(sz);
                    fread(&r.file_data[0], 1, sz, fp);
                    fclose(fp);
                }
            }
        } else {
            r.error_message = "文件不存在";
        }
        return r;
    }

    QueryResult handle_file_upload_status_query(const Message& msg) {
        QueryResult r;
        // payload 格式: "file_name\nfile_size"
        auto [file_name, size_str] = split_two(msg.payload);
        uint64_t file_size = std::stoull(size_str);

        uint32_t total_chunks = static_cast<uint32_t>(
            (file_size + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
        if (total_chunks == 0) total_chunks = 1;

        auto received = storage_->get_received_chunks(msg.sender_id, file_name, file_size);

        r.success = true;
        r.total_chunks = total_chunks;
        r.received_chunks = received;

        // 如果分片全部收齐，file_id 已存在则一并返回
        if (received.size() >= total_chunks) {
            // 检查文件是否已完成（MySQL 中有记录）
            std::string path = "/tmp/chatroom_files/" + file_name;
            FILE* fp = fopen(path.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                uint64_t actual_size = ftell(fp);
                fclose(fp);
                if (actual_size >= file_size) {
                    // 文件已完成，尝试获取已有的 file_id
                    // 通过扫描 MySQL 或直接返回 complete 状态
                    r.file_id = 0;  // 已完成但需重新确认
                }
            }
        }

        return r;
    }

    QueryResult handle_file_download_chunk_query(const Message& msg) {
        QueryResult r;
        uint64_t fid = msg.target_id; if (fid == 0) fid = std::stoull(msg.payload);
        auto info = storage_->get_file_info(fid);
        if (info.file_id == 0) return fail("文件不存在");

        uint32_t total_chunks = static_cast<uint32_t>(
            (info.file_size + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE);
        if (total_chunks == 0) total_chunks = 1;

        uint64_t offset = static_cast<uint64_t>(msg.chunk_seq) * FILE_CHUNK_SIZE;
        uint64_t chunk_size = std::min(static_cast<uint64_t>(FILE_CHUNK_SIZE),
                                        info.file_size - offset);

        FILE* fp = fopen(info.file_path.c_str(), "rb");
        if (!fp) return fail("无法打开文件");
        fseek(fp, offset, SEEK_SET);
        r.file_data.resize(chunk_size);
        fread(&r.file_data[0], 1, chunk_size, fp);
        fclose(fp);

        r.success = true;
        r.file_id = fid;
        r.file_name = info.file_name;
        r.file_size = info.file_size;
        r.chunk_seq = msg.chunk_seq;
        r.total_chunks = total_chunks;
        return r;
    }

    std::shared_ptr<StorageManager> storage_;
};

} // namespace chatroom