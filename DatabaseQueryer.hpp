#pragma once

#include "MessageType.hpp"
#include "SessionState.hpp"
#include "DatabaseQueryResult.hpp"
#include "StorageManager.hpp"
#include <memory>
#include <iostream>
#include <utility>
#include <sys/stat.h>

namespace chatroom {

class DatabaseQueryer {
public:
    explicit DatabaseQueryer(std::shared_ptr<StorageManager> storage)
        : storage_(storage) {}

    QueryResult query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        int type_val = static_cast<int>(msg.type);

        if (type_val >= 1 && type_val < 100) {
            return query_account(conn_state, msg);
        } else if (type_val >= 100 && type_val < 200) {
            return query_friend(conn_state, msg);
        } else if (type_val >= 200 && type_val < 300) {
            return query_group(conn_state, msg);
        } else if (type_val >= 300 && type_val < 400) {
            return query_chat(conn_state, msg);
        } else if (type_val >= 400 && type_val < 500) {
            return query_file(conn_state, msg);
        }

        result.success = false;
        result.error_message = "未知消息类型";
        return result;
    }

private:
    static std::pair<std::string, std::string> split_two(const std::string& s) {
        size_t pos = s.find('\n');
        if (pos == std::string::npos) return {s, ""};
        return {s.substr(0, pos), s.substr(pos + 1)};
    }

    // ===== 账号模块 =====
    QueryResult query_account(SessionState conn_state, const Message& msg) {
        switch (msg.type) {
            case MessageType::LOGIN_REQ:         return handle_login_query(conn_state, msg);
            case MessageType::REGISTER_REQ:      return handle_register_query(conn_state, msg);
            case MessageType::LOGOUT_REQ:        return handle_logout_query(conn_state, msg);
            case MessageType::VERIFY_CODE_REQ:   return handle_verify_code_query(conn_state, msg);
            case MessageType::PASSWORD_RESET_REQ:return handle_password_reset_query(conn_state, msg);
            default: {
                QueryResult r; r.success = false;
                r.error_message = "未知账号操作"; return r;
            }
        }
    }

    QueryResult handle_login_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (conn_state == SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "您已登录，请勿重复登录";
            return result;
        }

        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        auto [username, password] = split_two(msg.payload);

        if (!storage_->verify_password(username, password)) {
            result.success = false;
            result.error_message = "用户名或密码错误";
            return result;
        }

        auto user_result = storage_->get_user_by_username(username);
        if (!user_result.success) return user_result;

        std::string token = storage_->create_session(user_result.user_id, username);
        storage_->set_online(user_result.user_id);

        user_result.friend_list = storage_->get_friends(user_result.user_id);
        user_result.token = token;
        user_result.offline_messages = storage_->get_offline_messages(user_result.user_id);
        return user_result;
    }

    QueryResult handle_register_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        auto [username, rest] = split_two(msg.payload);
        auto [password, nickname] = split_two(rest);
        if (nickname.empty()) nickname = username;

        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        return storage_->create_user(username, password, nickname);
    }

    QueryResult handle_logout_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        if (msg.sender_id != 0) {
            storage_->clear_session(msg.sender_id);
            storage_->set_offline(msg.sender_id);
        }
        result.success = true;
        return result;
    }

    QueryResult handle_verify_code_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        result.success = true;
        result.verify_code = "123456";
        return result;
    }

    QueryResult handle_password_reset_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        auto [user_id_str, new_password] = split_two(msg.payload);
        uint64_t user_id = std::stoull(user_id_str);
        bool ok = storage_->update_password(user_id, new_password);
        result.success = ok;
        if (!ok) result.error_message = "密码重置失败";
        return result;
    }

    // ===== 好友模块 =====
    QueryResult query_friend(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }

        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        switch (msg.type) {
            case MessageType::ADD_FRIEND_REQ:     return handle_add_friend_query(conn_state, msg);
            case MessageType::DELETE_FRIEND_REQ:  return handle_delete_friend_query(conn_state, msg);
            case MessageType::QUERY_FRIEND_REQ:   return handle_query_friend_query(conn_state, msg);
            case MessageType::BLOCK_FRIEND_REQ:   return handle_block_friend_query(conn_state, msg);
            default: { result.success = false; result.error_message = "未知好友操作"; return result; }
        }
    }

    QueryResult handle_add_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t target_id = msg.target_id;
        if (target_id == 0) target_id = std::stoull(msg.payload);

        if (!storage_->get_user_by_id(target_id).success) {
            result.success = false;
            result.error_message = "目标用户不存在";
            return result;
        }

        if (storage_->is_friend(msg.sender_id, target_id)) {
            result.success = false;
            result.error_message = "已经是好友了";
            return result;
        }

        result.success = storage_->add_friend(msg.sender_id, target_id);
        if (!result.success) result.error_message = "添加好友失败";
        return result;
    }

    QueryResult handle_delete_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t target_id = msg.target_id;
        if (target_id == 0) target_id = std::stoull(msg.payload);
        result.success = storage_->remove_friend(msg.sender_id, target_id);
        if (!result.success) result.error_message = "删除好友失败";
        return result;
    }

    QueryResult handle_query_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        result.success = true;
        result.friend_list = storage_->get_friends(msg.sender_id);
        return result;
    }

    QueryResult handle_block_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t target_id = msg.target_id;
        if (target_id == 0) target_id = std::stoull(msg.payload);
        result.success = storage_->block_friend(msg.sender_id, target_id);
        if (!result.success) result.error_message = "屏蔽失败";
        return result;
    }

    // ===== 群组模块 =====
    QueryResult query_group(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }

        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        switch (msg.type) {
            case MessageType::CREATE_GROUP_REQ:          return handle_create_group_query(conn_state, msg);
            case MessageType::DISMISS_GROUP_REQ:         return handle_dismiss_group_query(conn_state, msg);
            case MessageType::JOIN_GROUP_REQ:            return handle_join_group_query(conn_state, msg);
            case MessageType::QUIT_GROUP_REQ:            return handle_quit_group_query(conn_state, msg);
            case MessageType::QUERY_GROUP_LIST_REQ:      return handle_query_group_list_query(conn_state, msg);
            case MessageType::QUERY_GROUP_MEMBERS_REQ:   return handle_query_group_members_query(conn_state, msg);
            default: { result.success = false; result.error_message = "未知群组操作"; return result; }
        }
    }

    QueryResult handle_create_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        auto [group_name, description] = split_two(msg.payload);

        uint64_t group_id = storage_->create_group(group_name, description, msg.sender_id);
        result.success = (group_id != 0);
        if (result.success) {
            result.group_id = group_id;
        } else {
            result.error_message = "创建群组失败";
        }
        return result;
    }

    QueryResult handle_dismiss_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t group_id = msg.group_id;
        if (group_id == 0) group_id = std::stoull(msg.payload);
        result.success = storage_->dismiss_group(group_id, msg.sender_id);
        if (!result.success) result.error_message = "解散群组失败";
        return result;
    }

    QueryResult handle_join_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t group_id = msg.group_id;
        if (group_id == 0) group_id = std::stoull(msg.payload);

        if (storage_->is_group_member(group_id, msg.sender_id)) {
            result.success = false;
            result.error_message = "已经是群组成员";
            return result;
        }

        result.success = storage_->join_group(group_id, msg.sender_id);
        if (result.success) {
            result.group_id = group_id;
        } else {
            result.error_message = "加入群组失败";
        }
        return result;
    }

    QueryResult handle_quit_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t group_id = msg.group_id;
        if (group_id == 0) group_id = std::stoull(msg.payload);
        result.success = storage_->quit_group(group_id, msg.sender_id);
        if (!result.success) result.error_message = "退出群组失败";
        return result;
    }

    QueryResult handle_query_group_list_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        result.success = true;
        result.group_list = storage_->get_user_groups(msg.sender_id);
        return result;
    }

    QueryResult handle_query_group_members_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t group_id = msg.group_id;
        if (group_id == 0) group_id = std::stoull(msg.payload);

        if (!storage_->is_group_member(group_id, msg.sender_id)) {
            result.success = false;
            result.error_message = "你不是该群组成员";
            return result;
        }

        result.success = true;
        result.group_members = storage_->get_group_members(group_id);
        return result;
    }

    // ===== 聊天模块 =====
    QueryResult query_chat(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }

        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        switch (msg.type) {
            case MessageType::PRIVATE_CHAT_REQ:  return handle_private_chat_query(conn_state, msg);
            case MessageType::GROUP_CHAT_REQ:    return handle_group_chat_query(conn_state, msg);
            case MessageType::GET_HISTORY_REQ:   return handle_get_history_query(conn_state, msg);
            default: { result.success = false; result.error_message = "未知聊天操作"; return result; }
        }
    }

    QueryResult handle_private_chat_query(SessionState conn_state, const Message& msg) {
        QueryResult result;

        if (!storage_->is_friend(msg.sender_id, msg.target_id)) {
            result.success = false;
            result.error_message = "不是好友，无法发送私聊";
            return result;
        }

        storage_->save_message(msg.sender_id, msg.target_id, msg.payload,
                               static_cast<int>(MessageType::PRIVATE_CHAT_REQ));

        if (!storage_->is_online(msg.target_id)) {
            auto sender = storage_->get_user_by_id(msg.sender_id);
            std::string name = sender.success ? sender.username
                               : std::to_string(msg.sender_id);
            storage_->save_offline_message(msg.target_id, msg.sender_id, name, msg.payload);
        }

        result.success = true;
        return result;
    }

    QueryResult handle_group_chat_query(SessionState conn_state, const Message& msg) {
        QueryResult result;

        if (!storage_->is_group_member(msg.group_id, msg.sender_id)) {
            result.success = false;
            result.error_message = "你不在该群组中";
            return result;
        }

        storage_->save_group_message(msg.group_id, msg.sender_id, msg.payload);

        result.success = true;
        result.group_members = storage_->get_group_members(msg.group_id);
        return result;
    }

    QueryResult handle_get_history_query(SessionState conn_state, const Message& msg) {
        QueryResult result;

        auto [target_str, rest] = split_two(msg.payload);
        auto [group_str, limit_str] = split_two(rest);
        uint64_t target_id = target_str.empty() ? 0 : std::stoull(target_str);
        uint64_t group_id = group_str.empty() ? 0 : std::stoull(group_str);
        int limit = limit_str.empty() ? 50 : std::stoi(limit_str);

        if (group_id != 0) {
            result.history = storage_->get_group_history(group_id, limit);
        } else if (target_id != 0) {
            result.history = storage_->get_history(msg.sender_id, target_id, limit);
        } else {
            result.success = false;
            result.error_message = "缺少查询参数";
            return result;
        }

        result.success = true;
        return result;
    }

    // ===== 文件模块 =====
    QueryResult query_file(SessionState conn_state, const Message& msg) {
        QueryResult result;
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }

        if (!storage_) {
            result.success = false;
            result.error_message = "存储服务未就绪";
            return result;
        }

        switch (msg.type) {
            case MessageType::FILE_UPLOAD_REQ:   return handle_file_upload_query(conn_state, msg);
            case MessageType::FILE_DOWNLOAD_REQ:  return handle_file_download_query(conn_state, msg);
            default: { result.success = false; result.error_message = "未知文件操作"; return result; }
        }
    }

    QueryResult handle_file_upload_query(SessionState conn_state, const Message& msg) {
        QueryResult result;

        // 创建文件存储目录
        std::string file_dir = "/tmp/chatroom_files/";
        mkdir(file_dir.c_str(), 0755);
        
        std::string file_path = file_dir + msg.payload;
        
        // 如果是完整文件（chunk_seq == 0）
        if (msg.chunk_seq == 0) {
            // 直接写入整个文件
            FILE* fp = fopen(file_path.c_str(), "wb");
            if (!fp) {
                result.success = false;
                result.error_message = "无法创建文件";
                return result;
            }
            fwrite(msg.file_data.data(), 1, msg.file_data.size(), fp);
            fclose(fp);
        }

        uint64_t file_id = storage_->save_file_metadata(
            msg.payload, msg.file_size, file_path,
            msg.sender_id, msg.target_id);

        result.success = (file_id != 0);
        result.file_id = file_id;
        result.file_name = msg.payload;
        result.file_size = msg.file_size;
        
        if (!result.success) result.error_message = "文件上传失败";
        return result;
    }

    QueryResult handle_file_download_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        uint64_t file_id = msg.target_id;
        if (file_id == 0) file_id = std::stoull(msg.payload);

        auto file_info = storage_->get_file_info(file_id);
        if (file_info.file_id == 0) {
            result.success = false;
            result.error_message = "文件不存在";
            return result;
        }

        result.success = true;
        result.offline_files.push_back(file_info);
        return result;
    }

private:
    std::shared_ptr<StorageManager> storage_;
};

} // namespace chatroom