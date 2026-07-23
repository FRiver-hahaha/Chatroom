#pragma once

#include "MessageType.hpp"
#include "SessionState.hpp"
#include "DatabaseQueryResult.hpp"
#include <memory>
#include <iostream>

namespace chatroom {

class StorageManager {
public:
    // MySQL/Redis
};


// 数据库查询器
class DatabaseQueryer {
public:
    explicit DatabaseQueryer(std::shared_ptr<StorageManager> storage)
        : storage_(storage) {}
    
    /**
     * @brief 综合用户状态和消息类型，执行数据库查询
     * @param conn_state 当前连接状态
     * @param msg 消息结构
     * @return 查询结果
     */
    QueryResult query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== ① 根据消息类型分发查询 =====
        int type_val = static_cast<int>(msg.type);
        
        if (type_val >= 1 && type_val < 100) {
            // 账号模块
            return query_account(conn_state, msg);
        } else if (type_val >= 100 && type_val < 200) {
            // 好友模块
            return query_friend(conn_state, msg);
        } else if (type_val >= 200 && type_val < 300) {
            // 群组模块
            return query_group(conn_state, msg);
        } else if (type_val >= 300 && type_val < 400) {
            // 聊天模块
            return query_chat(conn_state, msg);
        } else if (type_val >= 400 && type_val < 500) {
            // 文件模块
            return query_file(conn_state, msg);
        }
        
        result.success = false;
        result.error_message = "未知消息类型";
        return result;
    }

private:
    // ===== 账号模块查询 =====
    QueryResult query_account(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        switch (msg.type) {
            case MessageType::LOGIN_REQ:
                return handle_login_query(conn_state, msg);
            case MessageType::REGISTER_REQ:
                return handle_register_query(conn_state, msg);
            case MessageType::LOGOUT_REQ:
                return handle_logout_query(conn_state, msg);
            case MessageType::VERIFY_CODE_REQ:
                return handle_verify_code_query(conn_state, msg);
            case MessageType::PASSWORD_RESET_REQ:
                return handle_password_reset_query(conn_state, msg);
            default:
                result.success = false;
                result.error_message = "未知账号操作";
                return result;
        }
    }
    
    QueryResult handle_login_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 检查当前状态 =====
        if (conn_state == SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "您已登录，请勿重复登录";
            return result;
        }
        
        // ===== 查询用户信息 =====
        // 实际从 MySQL 查询
        // UserInfo user = storage_->get_user_by_username(username);
        // if (!user.valid) { ... }
        
        // 模拟查询
        result.success = true;
        result.user_id = 1001;
        result.username = "test_user";
        result.nickname = "测试用户";
        result.token = "generated_token_xxx";
        
        // ===== 如果是重连场景（从 token 恢复） =====
        if (conn_state == SessionState::RECONNECTING) {
            // 从 Redis 验证 token
            // bool valid = storage_->verify_token(msg.token);
            result.success = true;
        }
        
        return result;
    }
    
    QueryResult handle_register_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 检查用户名是否已存在 =====
        // bool exists = storage_->user_exists(username);
        // if (exists) { ... }
        
        // ===== 创建新用户 =====
        // storage_->create_user(user_info);
        
        result.success = true;
        result.user_id = 1002;
        result.username = "new_user";
        result.token = "generated_token_yyy";
        
        return result;
    }
    
    QueryResult handle_logout_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 清除会话 =====
        // storage_->clear_session(msg.token);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_verify_code_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 生成验证码（提高） =====
        // 发送邮件/短信
        
        result.success = true;
        result.verify_code = "123456";
        return result;
    }
    
    QueryResult handle_password_reset_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 验证验证码，重置密码 =====
        // bool valid = storage_->verify_code(msg.payload);
        // if (valid) { storage_->reset_password(msg.target_id, new_password); }
        
        result.success = true;
        return result;
    }
    
    // ===== 好友模块查询 =====
    QueryResult query_friend(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // 检查是否登录
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }
        
        switch (msg.type) {
            case MessageType::ADD_FRIEND_REQ:
                return handle_add_friend_query(conn_state, msg);
            case MessageType::DELETE_FRIEND_REQ:
                return handle_delete_friend_query(conn_state, msg);
            case MessageType::QUERY_FRIEND_REQ:
                return handle_query_friend_query(conn_state, msg);
            case MessageType::BLOCK_FRIEND_REQ:
                return handle_block_friend_query(conn_state, msg);
            default:
                result.success = false;
                result.error_message = "未知好友操作";
                return result;
        }
    }
    
    QueryResult handle_add_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 检查目标用户是否存在 =====
        // UserInfo target = storage_->get_user_by_id(msg.target_id);
        // if (!target.valid) { ... }
        
        // ===== 检查是否已经是好友 =====
        // bool is_friend = storage_->is_friend(msg.sender_id, msg.target_id);
        // if (is_friend) { ... }
        
        // ===== 添加好友关系 =====
        // storage_->add_friend(msg.sender_id, msg.target_id);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_delete_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 删除好友关系 =====
        // storage_->remove_friend(msg.sender_id, msg.target_id);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_query_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 查询好友列表 =====
        // auto friends = storage_->get_friends(msg.sender_id);
        // for (auto& f : friends) { result.friend_list.push_back({...}); }
        
        // 模拟数据
        result.success = true;
        result.friend_list = {
            {1002, "user2", "小明", true, false, 1234567890},
            {1003, "user3", "小红", false, false, 1234567891},
        };
        
        return result;
    }
    
    QueryResult handle_block_friend_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 屏蔽好友 =====
        // storage_->block_friend(msg.sender_id, msg.target_id);
        
        result.success = true;
        return result;
    }
    
    // ===== 群组模块查询 =====
    QueryResult query_group(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }
        
        switch (msg.type) {
            case MessageType::CREATE_GROUP_REQ:
                return handle_create_group_query(conn_state, msg);
            case MessageType::JOIN_GROUP_REQ:
                return handle_join_group_query(conn_state, msg);
            case MessageType::QUIT_GROUP_REQ:
                return handle_quit_group_query(conn_state, msg);
            case MessageType::QUERY_GROUP_LIST_REQ:
                return handle_query_group_list_query(conn_state, msg);
            case MessageType::QUERY_GROUP_MEMBERS_REQ:
                return handle_query_group_members_query(conn_state, msg);
            default:
                result.success = false;
                result.error_message = "未知群组操作";
                return result;
        }
    }
    
    QueryResult handle_create_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 创建群组 =====
        // GroupInfo group;
        // group.group_name = "新群组";
        // group.owner_id = msg.sender_id;
        // uint64_t group_id = storage_->create_group(group);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_join_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 申请加入群组 =====
        // 检查群组是否存在
        // 检查是否已经是成员
        // 如果是公开群，直接加入；如果是私有群，需要管理员审批
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_quit_group_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 退出群组 =====
        // storage_->remove_group_member(msg.group_id, msg.sender_id);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_query_group_list_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 查询用户加入的群组 =====
        // auto groups = storage_->get_user_groups(msg.sender_id);
        
        result.success = true;
        result.group_list = {
            {2001, "技术交流群", "C++/Python/Go", 1001, 50, true},
            {2002, "闲聊群", "随便聊聊", 1003, 30, true},
        };
        
        return result;
    }
    
    QueryResult handle_query_group_members_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 查询群组成员 =====
        // auto members = storage_->get_group_members(msg.group_id);
        
        result.success = true;
        result.group_members = {
            {1001, "user1", "群主", "owner", 1234567890},
            {1002, "user2", "管理员", "admin", 1234567891},
            {1003, "user3", "普通成员", "member", 1234567892},
        };
        
        return result;
    }
    
    // ===== 聊天模块查询 =====
    QueryResult query_chat(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }
        
        switch (msg.type) {
            case MessageType::PRIVATE_CHAT_REQ:
                return handle_private_chat_query(conn_state, msg);
            case MessageType::GROUP_CHAT_REQ:
                return handle_group_chat_query(conn_state, msg);
            case MessageType::GET_HISTORY_REQ:
                return handle_get_history_query(conn_state, msg);
            default:
                result.success = false;
                result.error_message = "未知聊天操作";
                return result;
        }
    }
    
    QueryResult handle_private_chat_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 检查好友关系 =====
        // if (!storage_->is_friend(msg.sender_id, msg.target_id)) {
        //     result.success = false;
        //     result.error_message = "不是好友关系";
        //     return result;
        // }
        
        // ===== 检查是否被屏蔽 =====
        // if (storage_->is_blocked(msg.target_id, msg.sender_id)) {
        //     result.success = false;
        //     result.error_message = "你被对方屏蔽了";
        //     return result;
        // }
        
        // ===== 存储消息 =====
        // storage_->save_message(msg.sender_id, msg.target_id, msg.payload, 1);
        
        // ===== 检查目标用户在线状态 =====
        // bool online = storage_->is_online(msg.target_id);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_group_chat_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 检查是否在群组中 =====
        // if (!storage_->is_group_member(msg.group_id, msg.sender_id)) {
        //     result.success = false;
        //     result.error_message = "你不在该群组中";
        //     return result;
        // }
        
        // ===== 存储群消息 =====
        // storage_->save_group_message(msg.group_id, msg.sender_id, msg.payload);
        
        // ===== 获取群组成员列表（用于广播） =====
        // auto members = storage_->get_group_members(msg.group_id);
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_get_history_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 查询历史消息 =====
        // auto history = storage_->get_history(msg.sender_id, msg.target_id, 50);
        
        result.success = true;
        result.history = {
            {1, 1002, "小明", "你好！", 1234567890, true},
            {2, 1001, "自己", "你好！很高兴认识你", 1234567891, true},
        };
        
        return result;
    }
    
    // ===== 文件模块查询 =====
    QueryResult query_file(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        if (conn_state != SessionState::LOGGED_IN) {
            result.success = false;
            result.error_message = "请先登录";
            return result;
        }
        
        switch (msg.type) {
            case MessageType::FILE_UPLOAD_REQ:
                return handle_file_upload_query(conn_state, msg);
            case MessageType::FILE_DOWNLOAD_REQ:
                return handle_file_download_query(conn_state, msg);
            default:
                result.success = false;
                result.error_message = "未知文件操作";
                return result;
        }
    }
    
    QueryResult handle_file_upload_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 保存文件元数据到 MySQL =====
        // FileInfo file;
        // file.file_name = "photo.jpg";
        // file.file_size = 1024000;
        // file.sender_id = msg.sender_id;
        // storage_->save_file_metadata(file);
        // 文件内容保存到磁盘
        
        result.success = true;
        return result;
    }
    
    QueryResult handle_file_download_query(SessionState conn_state, const Message& msg) {
        QueryResult result;
        
        // ===== 获取文件信息 =====
        // auto file = storage_->get_file_info(msg.target_id);
        
        result.success = true;
        return result;
    }

private:
    std::shared_ptr<StorageManager> storage_;
};

} // namespace chatroom