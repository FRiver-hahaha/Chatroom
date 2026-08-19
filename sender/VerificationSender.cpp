#include "sender/VerificationSender.hpp"

#include <glog/logging.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <ctime>

namespace chatroom {

namespace {

constexpr int SMTP_PORT = 465;  // 隐式 SSL 端口

std::string base64_encode(const std::string& in) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(in[i])) << 16;
        if (i + 1 < in.size())
            n |= static_cast<uint32_t>(static_cast<unsigned char>(in[i + 1])) << 8;
        if (i + 2 < in.size())
            n |= static_cast<uint32_t>(static_cast<unsigned char>(in[i + 2]));
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(i + 1 < in.size() ? tbl[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < in.size() ? tbl[n & 63] : '=');
    }
    return out;
}

// 发送一行命令并读取一行响应，返回 3 位状态码（220/235/250 等），失败返回 -1
int smtp_command(BIO* bio, const std::string& cmd, std::string* reply) {
    std::string line = cmd + "\r\n";
    if (BIO_write(bio, line.data(), static_cast<int>(line.size())) <= 0) return -1;
    char buf[4096];
    int n = BIO_read(bio, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    std::string resp(buf, n);
    if (reply) *reply = resp;
    if (resp.size() < 3) return -1;
    return std::atoi(resp.substr(0, 3).c_str());
}

} // namespace

std::string generate_verify_code() {
    unsigned char buf[4];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        uint32_t t = static_cast<uint32_t>(time(nullptr));
        buf[0] = static_cast<unsigned char>(t >> 24);
        buf[1] = static_cast<unsigned char>(t >> 16);
        buf[2] = static_cast<unsigned char>(t >> 8);
        buf[3] = static_cast<unsigned char>(t);
    }
    uint32_t v = (static_cast<uint32_t>(buf[0]) << 24) |
                 (static_cast<uint32_t>(buf[1]) << 16) |
                 (static_cast<uint32_t>(buf[2]) << 8) |
                 static_cast<uint32_t>(buf[3]);
    return std::to_string(100000 + v % 900000);
}

bool VerificationSender::send(const std::string& channel, const std::string& target,
                              const std::string& code) {
    if (channel != "email") {
        LOG(ERROR) << "[验证码] 不支持的渠道: " << channel;
        return false;
    }
    return send_email(target, code);
}

bool VerificationSender::send_email(const std::string& to, const std::string& code) {
    // 连接 SMTP 服务器（隐式 SSL）
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG(ERROR) << "[验证码] 创建 socket 失败";
        return false;
    }
    struct hostent* hp = gethostbyname(cfg_.host.c_str());
    if (!hp) {
        close(fd);
        LOG(ERROR) << "[验证码] 无法解析 SMTP 主机: " << cfg_.host;
        return false;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SMTP_PORT);
    memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        LOG(ERROR) << "[验证码] 连接 SMTP 失败: " << strerror(errno);
        return false;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); return false; }
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    bool ok = (SSL_connect(ssl) == 1);
    BIO* bio = BIO_new(BIO_f_ssl());
    BIO_set_ssl(bio, ssl, BIO_CLOSE);

    std::string reply;
    if (ok) {
        char buf[4096];
        int n = BIO_read(bio, buf, sizeof(buf) - 1);
        if (n > 0) {
            reply.assign(buf, n);
            ok = (reply.size() >= 3 && std::atoi(reply.substr(0, 3).c_str()) == 220);
        } else {
            ok = false;
        }
    }
    if (ok) ok = (smtp_command(bio, "EHLO chatroom.local", &reply) / 100 == 2);
    if (ok) ok = (smtp_command(bio, "AUTH LOGIN", &reply) / 100 == 3);
    if (ok) ok = (smtp_command(bio, base64_encode(cfg_.from), &reply) / 100 == 3);
    if (ok) ok = (smtp_command(bio, base64_encode(cfg_.auth_code), &reply) / 100 == 2);
    if (ok) ok = (smtp_command(bio, "MAIL FROM:<" + cfg_.from + ">", &reply) / 100 == 2);
    if (ok) ok = (smtp_command(bio, "RCPT TO:<" + to + ">", &reply) / 100 == 2);
    if (ok) ok = (smtp_command(bio, "DATA", &reply) / 100 == 3);
    if (ok) {
        std::string body =
            "From: ChatRoom <" + cfg_.from + ">\r\n"
            "To: <" + to + ">\r\n"
            "Subject: ChatRoom 验证码\r\n"
            "MIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: 8bit\r\n"
            "\r\n"
            "亲爱的用户您好～\r\n"
            "您的验证码是: " + code + "\r\n"
            "在 5 分钟内有效，请勿泄露给其他人～\r\n"
            ".\r\n";
        ok = (BIO_write(bio, body.data(), static_cast<int>(body.size())) > 0);
        if (ok) ok = (smtp_command(bio, "", &reply) / 100 == 2);
    }
    smtp_command(bio, "QUIT", nullptr);

    BIO_free_all(bio);
    SSL_CTX_free(ctx);
    close(fd);

    if (!ok) LOG(ERROR) << "[验证码] 邮件发送失败, SMTP 响应: " << reply;
    else LOG(INFO) << "[验证码] 已发送到邮箱: " << to;
    return ok;
}

} // namespace chatroom
