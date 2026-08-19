#pragma once

#include <string>

namespace chatroom {

// 最简 SMTP 配置：仅需服务器地址、发件邮箱、授权码
struct SmtpConfig {
    std::string host;       // SMTP 服务器地址，如 smtp.qq.com
    std::string from;       // 发件邮箱，如 xxxxxx@qq.com
    std::string auth_code;  // 邮箱授权码（唯一需要配置的密钥，非登录密码）
};

// 生成 6 位数字验证码
std::string generate_verify_code();

// 验证码发送器：仅支持邮箱渠道（SMTP SSL 465 端口 + AUTH LOGIN）
class VerificationSender {
public:
    explicit VerificationSender(const SmtpConfig& cfg) : cfg_(cfg) {}

    // 发送验证码到目标邮箱，channel 仅支持 "email"
    bool send(const std::string& channel, const std::string& target, const std::string& code);

    // 邮箱渠道是否可用：host/from/auth_code 均已配置
    bool supports_channel(const std::string& channel) const {
        return channel == "email" && !cfg_.host.empty()
               && !cfg_.from.empty() && !cfg_.auth_code.empty();
    }

    bool debug_code_enabled() const { return false; }

private:
    bool send_email(const std::string& to, const std::string& code);

    SmtpConfig cfg_;
};

} // namespace chatroom
