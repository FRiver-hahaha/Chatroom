#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace chatroom {

enum class SessionState {
    NOT_LOGIN,          // 未登录
    LOGGED_IN,          // 已登录
    RECONNECTING,       // 重连中
    TOKEN_EXPIRED,      // token过期
    KICKED_OUT,         // 被踢下线（多端登录）
};

struct UserSession {
    uint64_t user_id = 0;
    std::string username;
    std::string nickname;
    SessionState state = SessionState::NOT_LOGIN;
    std::string token;
    std::chrono::steady_clock::time_point login_time;
    
    // 好友列表（缓存）
    std::vector<uint64_t> friend_ids;
    
    // 群组列表（缓存）
    std::vector<uint64_t> group_ids;
    
    bool is_online() const {
        return state == SessionState::LOGGED_IN || 
               state == SessionState::RECONNECTING;
    }
    
    bool is_authenticated() const {
        return state != SessionState::NOT_LOGIN;
    }
};

} // namespace chatroom