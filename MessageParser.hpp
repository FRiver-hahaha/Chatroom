#pragma once

#include "MessageType.hpp"
#include <string>
#include <cstring>
#include <iostream>

namespace chatroom {

class MessageParser {
public:
    static bool parse(const char* buffer, int len, Message& msg) {
        if (!buffer || len <= 0) {
            std::cerr << "[MessageParser] Invalid buffer" << std::endl;
            return false;
        }
        
        // ===== ① 解析 Protobuf =====
        // 注意：这里假设使用 Protobuf，实际需要 include 生成的 pb.h
        // chatroom::ChatMessage proto_msg;
        // if (!proto_msg.ParseFromArray(buffer, len)) {
        //     std::cerr << "[MessageParser] Protobuf parse failed" << std::endl;
        //     return false;
        // }
        
        // 暂时模拟：第一个字节是消息类型
        // 实际项目中替换为 Protobuf 解析
        uint8_t type_byte = static_cast<uint8_t>(buffer[0]);
        msg.type = static_cast<MessageType>(type_byte);
        
        // ===== ② 提取基本信息 =====
        // 实际从 Protobuf 中提取
        msg.sender_id = 0;      // 从 proto_msg.sender_id()
        msg.target_id = 0;      // 从 proto_msg.target_id()
        msg.group_id = 0;       // 从 proto_msg.group_id()
        msg.timestamp = 0;      // 从 proto_msg.timestamp()
        msg.token = "";         // 从 proto_msg.token()
        
        // ===== ③ 提取 payload =====
        // 实际从 Protobuf 中提取
        // msg.payload = proto_msg.payload();
        
        // ===== ④ 自动设置属性标志 =====
        msg.flags = get_flags_by_type(msg.type);
        
        return true;
    }
    
    /**
     * @brief 序列化消息为字节流
     * @param msg 消息结构
     * @return 序列化后的字符串
     */
    static std::string serialize(const Message& msg) {
        // 实际使用 Protobuf 序列化
        // chatroom::ChatMessage proto_msg;
        // proto_msg.set_type(static_cast<uint32_t>(msg.type));
        // proto_msg.set_sender_id(msg.sender_id);
        // proto_msg.set_target_id(msg.target_id);
        // proto_msg.set_payload(msg.payload);
        // proto_msg.set_timestamp(msg.timestamp);
        // return proto_msg.SerializeAsString();
        
        // 模拟：构造一个简单格式
        std::string result;
        result.push_back(static_cast<char>(msg.type));
        return result;
    }

private:
    static uint32_t get_flags_by_type(MessageType type) {
        uint32_t flags = 0;
        int type_val = static_cast<int>(type);
        
        //判断是请求还是响应
        if (type_val % 2 == 1) {
            flags |= (uint32_t)MessageFlag::IS_REQUEST;
        } else if (type_val % 2 == 0 && type_val != 0) {
            flags |= (uint32_t)MessageFlag::IS_RESPONSE;
        }
        

        if (type != MessageType::LOGIN_REQ &&
            type != MessageType::LOGIN_RSP &&
            type != MessageType::REGISTER_REQ &&
            type != MessageType::REGISTER_RSP &&
            type != MessageType::VERIFY_CODE_REQ &&
            type != MessageType::VERIFY_CODE_RSP &&
            type != MessageType::PASSWORD_RESET_REQ &&
            type != MessageType::PASSWORD_RESET_RSP) {
            flags |= (uint32_t)MessageFlag::NEED_LOGIN;
        }

        if (type_val >= 100 && type_val < 200) {
            // 好友模块
            flags |= (uint32_t)MessageFlag::NEED_FRIEND;
            flags |= (uint32_t)MessageFlag::NEED_DATABASE;
            flags |= (uint32_t)MessageFlag::NEED_PERMISSION;
        } else if (type_val >= 200 && type_val < 300) {
            // 群组模块
            flags |= (uint32_t)MessageFlag::NEED_GROUP;
            flags |= (uint32_t)MessageFlag::NEED_DATABASE;
            flags |= (uint32_t)MessageFlag::NEED_PERMISSION;
        } else if (type_val >= 300 && type_val < 400) {
            // 聊天模块
            flags |= (uint32_t)MessageFlag::NEED_DATABASE;
        } else if (type_val >= 400 && type_val < 500) {
            // 文件模块
            flags |= (uint32_t)MessageFlag::NEED_DATABASE;
        }
        
        return flags;
    }
};

} // namespace chatroom