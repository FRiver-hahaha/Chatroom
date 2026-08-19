#include "ClientState.h"
#include "state/MessageStore.h"
#include "network/ProtocolClient.h"
#include "service/MessageType.hpp"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>
#include <openssl/sha.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>

using chatroom::MessageType;

ClientState *ClientState::instance_ = nullptr;

static QString sha256Hex(const QByteArray &data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(data.constData()), data.size(), hash);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        oss << std::setw(2) << static_cast<int>(hash[i]);
    return QString::fromStdString(oss.str());
}
ClientState::ClientState(QObject *parent)
    : QObject(parent)
{
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(30000);
    connect(refresh_timer_, &QTimer::timeout, this, &ClientState::refreshContacts);

    login_timeout_timer_ = new QTimer(this);
    connect(login_timeout_timer_, &QTimer::timeout, this, &ClientState::onLoginTimeout);

    heartbeat_timer_ = new QTimer(this);
    heartbeat_timer_->setInterval(25000);
    connect(heartbeat_timer_, &QTimer::timeout, this, &ClientState::sendHeartbeat);
}

void ClientState::sendHeartbeat() {// 心跳，防止服务端空闲超时断开
    if (!isLoggedIn()) return;
    buildAndSend(static_cast<uint32_t>(MessageType::HEARTBEAT_REQ), nullptr);
}

void ClientState::refreshContacts() {
    if (isLoggedIn()) queryFriends();
}

ClientState *ClientState::instance() {
    if (!instance_) instance_ = new ClientState();
    return instance_;
}

void ClientState::setProtocolClient(ProtocolClient *client) {
    client_ = client;
    connect(client_, &ProtocolClient::connected, this, &ClientState::onConnected);
    connect(client_, &ProtocolClient::disconnected, this, &ClientState::onDisconnected);
    connect(client_, &ProtocolClient::messageReceived, this, &ClientState::onMessageReceived);
    connect(client_, &ProtocolClient::notificationReceived, this, &ClientState::onNotification);
}

// ===== internal helpers =====

void ClientState::buildAndSend(uint32_t type, const std::function<void(chatroom::ChatMessage &)> &setBody) {
    chatroom::ChatMessage msg;
    msg.set_type(type);
    msg.set_sender_id(user_id_);
    msg.set_timestamp(static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch()));
    if (!token_.isEmpty()) msg.set_token(token_.toStdString());
    if (setBody) setBody(msg);
    client_->sendMessage(msg);
}

void ClientState::storePendingCallback(uint32_t rspType, std::function<void(const chatroom::ChatMessage &)> cb) {
    pending_callbacks_[rspType] = std::move(cb);
}

// ===== account =====

void ClientState::login(const QString &username, const QString &password) {
    storePendingCallback(static_cast<uint32_t>(MessageType::LOGIN_RSP),
        [this](const chatroom::ChatMessage &msg) { handleLoginResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::LOGIN_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_login_req();
        body->set_username(username.toStdString());
        body->set_password(password.toStdString());
    });
    login_timeout_timer_->start(LoginTimeoutMs);
}

void ClientState::registerUser(const QString &username, const QString &password,
                               const QString &nickname, const QString &email,
                               const QString &verifyCode) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REGISTER_RSP),
        [this](const chatroom::ChatMessage &msg) { handleRegisterResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::REGISTER_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_register_req();
        body->set_username(username.toStdString());
        body->set_password(password.toStdString());
        body->set_nickname(nickname.toStdString());
        body->set_email(email.toStdString());
        body->set_verify_code(verifyCode.toStdString());
    });
    pending_nickname_ = nickname;
    login_timeout_timer_->start(LoginTimeoutMs);
}

void ClientState::sendVerifyCode(const QString &channel, const QString &target,
                                 const QString &scene) {
    storePendingCallback(static_cast<uint32_t>(MessageType::VERIFY_CODE_RSP),
        [this](const chatroom::ChatMessage &msg) { handleVerifyCodeResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::VERIFY_CODE_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_verify_code_req();
        body->set_channel(channel.toStdString());
        body->set_target(target.toStdString());
        body->set_scene(scene.toStdString());
    });
}

void ClientState::resetPassword(const QString &channel, const QString &target,
                                const QString &verifyCode, const QString &newPassword) {
    storePendingCallback(static_cast<uint32_t>(MessageType::PASSWORD_RESET_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit passwordResetResult(msg.password_reset_rsp().success(),
                                     QString::fromStdString(msg.password_reset_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::PASSWORD_RESET_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_password_reset_req();
        body->set_channel(channel.toStdString());
        body->set_target(target.toStdString());
        body->set_verify_code(verifyCode.toStdString());
        body->set_new_password(newPassword.toStdString());
    });
}

void ClientState::onLoginTimeout() {
    if (login_timeout_timer_->isActive()) {
        login_timeout_timer_->stop();
    }
    emit loginResult(false, "登录超时，请检查服务器地址和端口");
}

void ClientState::logout() {
    storePendingCallback(static_cast<uint32_t>(MessageType::LOGOUT_RSP),
        [this](const chatroom::ChatMessage &) {
            user_id_ = 0;
            username_.clear();
            nickname_.clear();
            token_.clear();
            contacts_.clear();
            current_messages_.clear();
            refresh_timer_->stop();
            emit logoutDone();
        });
    buildAndSend(static_cast<uint32_t>(MessageType::LOGOUT_REQ), nullptr);
}

void ClientState::deleteAccount(const QString &password) {
    storePendingCallback(static_cast<uint32_t>(MessageType::DELETE_ACCOUNT_RSP),
        [this](const chatroom::ChatMessage &msg) {
            bool ok = msg.delete_account_rsp().success();
            QString err = QString::fromStdString(msg.delete_account_rsp().error_message());
            if (ok) {
                user_id_ = 0; username_.clear(); nickname_.clear(); token_.clear();
                contacts_.clear(); current_messages_.clear();
                refresh_timer_->stop();
            }
            emit deleteAccountResult(ok, err);
        });
    buildAndSend(static_cast<uint32_t>(MessageType::DELETE_ACCOUNT_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_delete_account_req();
        body->set_password(password.toStdString());
    });
}

void ClientState::updateNickname(const QString &nickname) {
    storePendingCallback(static_cast<uint32_t>(MessageType::UPDATE_PROFILE_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.update_profile_rsp();
            if (rsp.success()) {
                nickname_ = QString::fromStdString(rsp.nickname());
            }
            emit operationResult(rsp.success(), QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::UPDATE_PROFILE_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_update_profile_req()->set_nickname(nickname.toStdString());
    });
}

// ===== friends =====

void ClientState::queryFriends() {
    storePendingCallback(static_cast<uint32_t>(MessageType::QUERY_FRIEND_RSP),
        [this](const chatroom::ChatMessage &msg) { handleFriendListResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::QUERY_FRIEND_REQ), nullptr);
}

void ClientState::addFriend(uint64_t targetUserId, const QString &targetEmail) {
    storePendingCallback(static_cast<uint32_t>(MessageType::ADD_FRIEND_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.add_friend_rsp().success(),
                                 QString::fromStdString(msg.add_friend_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::ADD_FRIEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetUserId);
        m.mutable_add_friend_req()->set_target_user_id(targetUserId);
        m.mutable_add_friend_req()->set_target_email(targetEmail.toStdString());
    });
}

void ClientState::deleteFriend(uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::DELETE_FRIEND_RSP),
        [this, targetUserId](const chatroom::ChatMessage &msg) {
            if (msg.delete_friend_rsp().success()) {
                contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
                    [targetUserId](const ContactItem &c) {
                        return c.type == ContactItem::Friend && c.id == targetUserId;
                    }), contacts_.end());
                if (current_chat_type_ == ChatType::Private && current_target_id_ == targetUserId)
                    current_messages_.clear();
                MessageStore::instance()->clearChat(false, targetUserId, 0);
                emit contactsUpdated();
            }
            emit operationResult(msg.delete_friend_rsp().success(),
                                 QString::fromStdString(msg.delete_friend_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::DELETE_FRIEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetUserId);
        m.mutable_delete_friend_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::blockFriend(uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::BLOCK_FRIEND_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.block_friend_rsp().success(),
                                 QString::fromStdString(msg.block_friend_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::BLOCK_FRIEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetUserId);
        m.mutable_block_friend_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::unblockFriend(uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::UNBLOCK_FRIEND_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.unblock_friend_rsp().success(),
                                 QString::fromStdString(msg.unblock_friend_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::UNBLOCK_FRIEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetUserId);
        m.mutable_unblock_friend_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::queryBlockedUsers() {
    storePendingCallback(static_cast<uint32_t>(MessageType::QUERY_BLOCKED_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.query_blocked_rsp();
            if (!rsp.success()) {
                emit operationResult(false,
                    QString::fromStdString(rsp.error_message()));
                return;
            }
            QVector<ContactItem> users;
            for (const auto &f : rsp.friends()) {
                ContactItem c;
                c.type = ContactItem::Friend;
                c.id = f.user_id();
                c.name = QString::fromStdString(f.nickname().empty() ? f.username() : f.nickname());
                c.is_online = f.is_online();
                c.add_time = f.add_time();
                users.append(c);
            }
            emit blockedUsersReceived(users);
        });
    buildAndSend(static_cast<uint32_t>(MessageType::QUERY_BLOCKED_REQ), nullptr);
}

// ===== groups =====

void ClientState::createGroup(const QString &name, const QString &desc, bool isPublic) {
    storePendingCallback(static_cast<uint32_t>(MessageType::CREATE_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
            if (msg.create_group_rsp().success()) {
                queryGroupList();  // 快速刷新群组列表
                emit systemNotification("群组创建成功");
            }
            emit operationResult(msg.create_group_rsp().success(),
                                 QString::fromStdString(msg.create_group_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::CREATE_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_create_group_req();
        body->set_group_name(name.toStdString());
        body->set_description(desc.toStdString());
        body->set_is_public(isPublic);
    });
}

void ClientState::joinGroup(uint64_t groupId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::JOIN_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.join_group_rsp();
            if (rsp.success()) {
                if (rsp.pending()) {
                    emit systemNotification("入群申请已发送，等待管理员审批");
                } else {
                    queryGroupList();  // 快速刷新群组列表
                    emit systemNotification("已加入群组");
                }
            }
            emit operationResult(rsp.success(),
                                 QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::JOIN_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_join_group_req()->set_group_id(groupId);
    });
}

void ClientState::quitGroup(uint64_t groupId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::QUIT_GROUP_RSP),
        [this, groupId](const chatroom::ChatMessage &msg) {
            if (msg.quit_group_rsp().success()) {
                contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
                    [groupId](const ContactItem &c) {
                        return c.type == ContactItem::Group && c.group_id == groupId;
                    }), contacts_.end());
                if (current_chat_type_ == ChatType::Group && current_group_id_ == groupId)
                    current_messages_.clear();
                MessageStore::instance()->clearChat(true, 0, groupId);
                emit contactsUpdated();
            }
            emit operationResult(msg.quit_group_rsp().success(),
                                 QString::fromStdString(msg.quit_group_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::QUIT_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_quit_group_req()->set_group_id(groupId);
    });
}

void ClientState::dismissGroup(uint64_t groupId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::DISMISS_GROUP_RSP),
        [this, groupId](const chatroom::ChatMessage &msg) {
            if (msg.dismiss_group_rsp().success()) {
                contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
                    [groupId](const ContactItem &c) {
                        return c.type == ContactItem::Group && c.group_id == groupId;
                    }), contacts_.end());
                emit contactsUpdated();
            }
            emit operationResult(msg.dismiss_group_rsp().success(),
                                 QString::fromStdString(msg.dismiss_group_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::DISMISS_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_dismiss_group_req()->set_group_id(groupId);
    });
}

void ClientState::queryGroupList() {
    storePendingCallback(static_cast<uint32_t>(MessageType::QUERY_GROUP_LIST_RSP),
        [this](const chatroom::ChatMessage &msg) { handleGroupListResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::QUERY_GROUP_LIST_REQ), nullptr);
}

void ClientState::queryGroupMembers(uint64_t groupId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::QUERY_GROUP_MEMBERS_RSP),
        [this](const chatroom::ChatMessage &msg) { handleGroupMembersResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::QUERY_GROUP_MEMBERS_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_query_group_members_req()->set_group_id(groupId);
    });
}

void ClientState::addGroupAdmin(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::ADD_GROUP_ADMIN_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.add_group_admin_rsp();
            if (rsp.success()) emitGroupMembersFromRsp(rsp.group_id(), rsp.members());
            emit operationResult(rsp.success(),
                                 QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::ADD_GROUP_ADMIN_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_add_group_admin_req()->set_group_id(groupId);
        m.mutable_add_group_admin_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::removeGroupAdmin(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REMOVE_GROUP_ADMIN_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.remove_group_admin_rsp();
            if (rsp.success()) emitGroupMembersFromRsp(rsp.group_id(), rsp.members());
            emit operationResult(rsp.success(),
                                 QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::REMOVE_GROUP_ADMIN_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_remove_group_admin_req()->set_group_id(groupId);
        m.mutable_remove_group_admin_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::approveJoinGroup(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::APPROVE_JOIN_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.approve_join_group_rsp();
            if (rsp.success()) emitGroupMembersFromRsp(rsp.group_id(), rsp.members());
            emit operationResult(rsp.success(),
                                 QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::APPROVE_JOIN_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_approve_join_group_req()->set_group_id(groupId);
        m.mutable_approve_join_group_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::removeGroupMember(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REMOVE_GROUP_MEMBER_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.remove_group_member_rsp();
            if (rsp.success()) emitGroupMembersFromRsp(rsp.group_id(), rsp.members());
            emit operationResult(rsp.success(),
                                 QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::REMOVE_GROUP_MEMBER_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_remove_group_member_req()->set_group_id(groupId);
        m.mutable_remove_group_member_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::rejectJoinGroup(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REJECT_JOIN_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.reject_join_group_rsp();
            if (rsp.success()) emitGroupMembersFromRsp(rsp.group_id(), rsp.members());
            emit operationResult(rsp.success(),
                                 QString::fromStdString(rsp.error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::REJECT_JOIN_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_reject_join_group_req()->set_group_id(groupId);
        m.mutable_reject_join_group_req()->set_target_user_id(targetUserId);
    });
}

// ===== chat =====

void ClientState::sendPrivateChat(uint64_t targetId, const QString &text) {
    MessageItem item;
    item.sender_id = user_id_;
    item.sender_name = nickname_.isEmpty() ? username_ : nickname_;
    item.content = text;
    item.timestamp = QDateTime::currentSecsSinceEpoch();
    item.is_self = true;
    item.status = MessageItem::Sending;
    item.msg_type = MessageItem::Text;
    current_messages_.append(item);
    emit messagesUpdated();

    buildAndSend(static_cast<uint32_t>(MessageType::PRIVATE_CHAT_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetId);
        m.mutable_private_chat_req()->set_payload(text.toStdString());
    });
    MessageStore::instance()->storeMessage(item, false, targetId, 0);
}

void ClientState::sendGroupChat(uint64_t groupId, const QString &text) {
    MessageItem item;
    item.sender_id = user_id_;
    item.sender_name = nickname_.isEmpty() ? username_ : nickname_;
    item.content = text;
    item.timestamp = QDateTime::currentSecsSinceEpoch();
    item.is_self = true;
    item.status = MessageItem::Sending;
    item.msg_type = MessageItem::Text;
    current_messages_.append(item);
    emit messagesUpdated();

    buildAndSend(static_cast<uint32_t>(MessageType::GROUP_CHAT_REQ), [&](chatroom::ChatMessage &m) {
        m.set_group_id(groupId);
        m.mutable_group_chat_req()->set_payload(text.toStdString());
    });
    MessageStore::instance()->storeMessage(item, true, 0, groupId);
}

void ClientState::getHistory(uint64_t targetId, uint64_t groupId, int limit) {
    storePendingCallback(static_cast<uint32_t>(MessageType::GET_HISTORY_RSP),
        [this](const chatroom::ChatMessage &msg) { handleHistoryResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::GET_HISTORY_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_get_history_req();
        body->set_target_user_id(targetId);
        body->set_group_id(groupId);
        body->set_limit(limit);
    });
}

// ===== file =====

void ClientState::sendFileRequest(uint64_t targetId, const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit operationResult(false, "Cannot open file: " + filePath);
        return;
    }

    QFileInfo info(filePath);
    QByteArray fileData = file.readAll();
    file.close();

    uint64_t fileSize = fileData.size();
    uint32_t totalChunks = (fileSize + 65535) / 65536; // 64KB chunks
    if (totalChunks == 0) totalChunks = 1;
    QByteArray fileHashRaw = sha256Hex(fileData).toUtf8();

    storePendingCallback(static_cast<uint32_t>(MessageType::FILE_SEND_RSP),
        [this, filePath, fileData, fileSize, totalChunks, fileHashRaw]
        (const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.file_send_rsp();
            if (!rsp.success()) {
                emit operationResult(false, QString::fromStdString(rsp.error_message()));
                return;
            }
            uint64_t transferId = rsp.transfer_id();
            QFileInfo info(filePath);
            // 创建成功后逐分片上传
            uploadNextChunk(transferId, info.fileName(), fileSize,
                            fileData, totalChunks, 0);
        });

    buildAndSend(static_cast<uint32_t>(MessageType::FILE_SEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetId);
        auto *body = m.mutable_file_send_req();
        body->set_file_name(info.fileName().toStdString());
        body->set_file_size(fileSize);
        body->set_total_chunks(totalChunks);
        body->set_file_hash(fileHashRaw.toStdString());
    });
}

void ClientState::uploadNextChunk(uint64_t transferId, const QString &fileName, uint64_t fileSize,
                                  const QByteArray &fileData, uint32_t totalChunks,
                                  uint32_t nextSeq) {
    if (nextSeq >= totalChunks) {
        // 全部分片上传完成，发送 finalize 让服务端组装文件
        QByteArray fileHashRaw = sha256Hex(fileData).toUtf8();
        storePendingCallback(static_cast<uint32_t>(MessageType::FILE_FINALIZE_RSP),
            [this, fileName](const chatroom::ChatMessage &msg) {
                const auto &rsp = msg.file_finalize_rsp();
                if (rsp.success()) {
                    emit operationResult(true, QString("文件已发送: %1").arg(fileName));
                } else {
                    emit operationResult(false, "文件组装失败: " +
                        QString::fromStdString(rsp.error_message()));
                }
            });
        buildAndSend(static_cast<uint32_t>(MessageType::FILE_FINALIZE_REQ), [&](chatroom::ChatMessage &m) {
            m.set_target_id(transferId);
            auto *body = m.mutable_file_finalize_req();
            body->set_transfer_id(transferId);
            body->set_file_hash(fileHashRaw.toStdString());
        });
        return;
    }

    uint64_t offset = static_cast<uint64_t>(nextSeq) * 65536;
    uint64_t chunkSize = qMin<uint64_t>(65536, fileSize - offset);
    QByteArray chunkData = fileData.mid(static_cast<int>(offset), static_cast<int>(chunkSize));
    QByteArray chunkHash = sha256Hex(chunkData).toUtf8();

    storePendingCallback(static_cast<uint32_t>(MessageType::FILE_SEND_CHUNK_RSP),
        [this, transferId, fileName, fileSize, fileData, totalChunks, nextSeq]
        (const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.file_send_chunk_rsp();
            if (!rsp.success()) {
                emit operationResult(false, QString("分片 %1 上传失败: %2")
                    .arg(nextSeq).arg(QString::fromStdString(rsp.error_message())));
                return;
            }
            uploadNextChunk(transferId, fileName, fileSize, fileData, totalChunks, nextSeq + 1);
        });
    buildAndSend(static_cast<uint32_t>(MessageType::FILE_SEND_CHUNK_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(transferId);  // 服务端用 envelope.target_id 作为 transfer_id
        auto *body = m.mutable_file_send_chunk_req();
        body->set_file_name(fileName.toStdString());
        body->set_file_size(fileSize);
        body->set_file_data(chunkData.constData(), static_cast<int>(chunkData.size()));
        body->set_chunk_seq(nextSeq);
        body->set_total_chunks(totalChunks);
        body->set_chunk_hash(chunkHash.toStdString());
    });
}

void ClientState::acceptFileTransfer(uint64_t transferId, bool accept) {
    buildAndSend(static_cast<uint32_t>(MessageType::FILE_TRANSFER_ACCEPT_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_file_transfer_accept_req();
        body->set_transfer_id(transferId);
        body->set_accept(accept);
    });
}

void ClientState::receiveFileChunks(uint64_t transferId, const QString &saveDir) {
    storePendingCallback(static_cast<uint32_t>(MessageType::FILE_TRANSFER_STATUS_RSP),
        [this, transferId, saveDir](const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.file_transfer_status_rsp();
            if (!rsp.success()) {
                emit operationResult(false, "无法查询传输状态: " +
                    QString::fromStdString(rsp.error_message()));
                return;
            }
            QString fileName = QString::fromStdString(rsp.file_name());
            QString savePath = saveDir;
            if (!savePath.isEmpty() && !savePath.endsWith('/')) savePath += '/';
            savePath += fileName;
            requestNextChunk(transferId, savePath, fileName, rsp.file_size(),
                             rsp.total_chunks(), 0);
        });
    buildAndSend(static_cast<uint32_t>(MessageType::FILE_TRANSFER_STATUS_REQ),
        [&](chatroom::ChatMessage &m) {
            m.set_target_id(transferId);
            m.mutable_file_transfer_status_req()->set_transfer_id(transferId);
        });
}

void ClientState::requestNextChunk(uint64_t transferId, const QString &savePath,
                                   const QString &fileName, uint64_t fileSize,
                                   uint32_t totalChunks, uint32_t nextSeq) {
    // 目标文件：首次打开时创建
    if (nextSeq == 0) {
        QFile f(savePath);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.close();
    }

    if (nextSeq >= totalChunks) {
        // 所有分片已写入，发送 finalize 让服务端校验并组装
        storePendingCallback(static_cast<uint32_t>(MessageType::FILE_FINALIZE_RSP),
            [this, fileName, savePath](const chatroom::ChatMessage &msg) {
                const auto &rsp = msg.file_finalize_rsp();
                emit operationResult(rsp.success(),
                    rsp.success() ? QString("文件已保存: %1").arg(savePath)
                                  : QString("文件接收失败: %1")
                                        .arg(QString::fromStdString(rsp.error_message())));
            });
        buildAndSend(static_cast<uint32_t>(MessageType::FILE_FINALIZE_REQ), [&](chatroom::ChatMessage &m) {
            m.set_target_id(transferId);
            auto *body = m.mutable_file_finalize_req();
            body->set_transfer_id(transferId);
        });
        return;
    }

    storePendingCallback(static_cast<uint32_t>(MessageType::FILE_RECEIVE_CHUNK_RSP),
        [this, transferId, savePath, fileName, fileSize, totalChunks, nextSeq]
        (const chatroom::ChatMessage &msg) {
            const auto &rsp = msg.file_receive_chunk_rsp();
            QByteArray data = QByteArray::fromStdString(rsp.file_data());
            if (data.isEmpty()) {
                emit operationResult(false, QString("分片 %1 接收失败").arg(nextSeq));
                return;
            }
            QFile f(savePath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
                f.write(data);
                f.close();
            } else {
                emit operationResult(false, "无法写入文件: " + savePath);
                return;
            }
            requestNextChunk(transferId, savePath, fileName, fileSize, totalChunks, nextSeq + 1);
        });
    buildAndSend(static_cast<uint32_t>(MessageType::FILE_RECEIVE_CHUNK_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(transferId);
        auto *body = m.mutable_file_receive_chunk_req();
        body->set_transfer_id(transferId);
        body->set_chunk_seq(nextSeq);
    });
}

// ===== state =====

void ClientState::setCurrentChat(ChatType type, uint64_t targetId, uint64_t groupId) {
    current_chat_type_ = type;
    current_target_id_ = targetId;
    current_group_id_ = groupId;
    current_messages_.clear();

    // 先加载本地 SQLite 记录，再向服务端拉取增量
    current_messages_ = MessageStore::instance()->loadHistory(
        type == ChatType::Group, targetId, groupId);
    emit currentChatChanged();

    if (type == ChatType::Private) {
        getHistory(targetId, 0);
    } else {
        getHistory(0, groupId);
    }
}

ContactItem ClientState::contactById(uint64_t id, ContactItem::Type type) const {
    for (const auto &c : contacts_) {
        if (c.type == type && c.id == id) return c;
    }
    return {};
}

// ===== response handlers =====

void ClientState::onMessageReceived(const chatroom::ChatMessage &msg) {
    auto msgType = static_cast<MessageType>(msg.type());

    // server pushes (incoming messages where sender != us)
    if (msgType == MessageType::PRIVATE_CHAT_RSP && msg.sender_id() != user_id_) {
        handleChatResponse(msg, true);
        return;
    }
    if (msgType == MessageType::GROUP_CHAT_RSP && msg.sender_id() != user_id_) {
        handleChatResponse(msg, false);
        return;
    }

    // file transfer notify
    if (msgType == MessageType::FILE_TRANSFER_NOTIFY) {
        handleFileTransferNotify(msg);
        return;
    }

    // group members changed: refresh group list (role/permissions) and current group members
    if (msgType == MessageType::GROUP_MEMBERS_CHANGED) {
        queryGroupList();
        if (current_chat_type_ == ChatType::Group &&
            current_group_id_ == msg.group_id()) {
            queryGroupMembers(msg.group_id());
        }
        return;
    }

    // heartbeat response: ignore
    if (msgType == MessageType::HEARTBEAT_RSP) {
        return;
    }

    // pending callback match
    auto it = pending_callbacks_.find(msg.type());
    if (it != pending_callbacks_.end()) {
        auto cb = it.value();
        pending_callbacks_.erase(it);
        cb(msg);
        return;
    }

    // verification code response
    if (msg.type() == static_cast<uint32_t>(MessageType::VERIFY_CODE_RSP)) {
        handleVerifyCodeResponse(msg);
        return;
    }
}

void ClientState::onNotification(const QString &text) {
    // parse system notifications
    if (text.contains("上线了")) {
        // update online status
        emit systemNotification(text);
    } else if (text.contains("请求添加你为好友")) {
        emit systemNotification(text);
    } else if (text.contains("离线消息")) {
        emit systemNotification(text);
    } else {
        emit systemNotification(text);
    }
}

void ClientState::onConnected() {
    emit operationResult(true, "Connected to server");
}

void ClientState::onDisconnected() {
    user_id_ = 0; username_.clear(); nickname_.clear(); token_.clear();
    contacts_.clear(); current_messages_.clear();
    refresh_timer_->stop();
    heartbeat_timer_->stop();
    emit logoutDone();
}

void ClientState::handleLoginResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.login_rsp();
    if (rsp.success()) {
        user_id_ = rsp.user_id();
        username_ = QString::fromStdString(rsp.username());
        nickname_ = QString::fromStdString(rsp.nickname());
        token_ = QString::fromStdString(rsp.token());
        queryFriends();
        queryGroupList();
        refresh_timer_->start();
        heartbeat_timer_->start();
        login_timeout_timer_->stop();
    }
    emit loginResult(rsp.success(), QString::fromStdString(rsp.error_message()));
}

void ClientState::handleRegisterResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.register_rsp();
    emit registerResult(rsp.success(), QString::fromStdString(rsp.error_message()));
    if (rsp.success()) {
        user_id_ = rsp.user_id();
        username_ = QString::fromStdString(rsp.username());
        nickname_ = pending_nickname_;
        token_ = QString::fromStdString(rsp.token());
        refresh_timer_->start();
    }
}

void ClientState::handleVerifyCodeResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.verify_code_rsp();
    emit verifyCodeResult(rsp.success(), QString::fromStdString(rsp.error_message()));
}

void ClientState::handleFriendListResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.query_friend_rsp();
    if (!rsp.success()) return;

    // keep group contacts, rebuild friend contacts
    contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
        [](const ContactItem &c) { return c.type == ContactItem::Friend; }),
        contacts_.end());

    for (const auto &f : rsp.friends()) {
        ContactItem item;
        item.type = ContactItem::Friend;
        item.id = f.user_id();
        item.name = QString::fromStdString(f.nickname().empty() ? f.username() : f.nickname());
        item.is_online = f.is_online();
        item.add_time = f.add_time();
        item.streak_days = f.streak_days();
        contacts_.append(item);
    }
    sortContacts();
    emit contactsUpdated();
}

void ClientState::handleGroupListResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.query_group_list_rsp();
    if (!rsp.success()) return;

    contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
        [](const ContactItem &c) { return c.type == ContactItem::Group; }),
        contacts_.end());

    for (const auto &g : rsp.groups()) {
        ContactItem item;
        item.type = ContactItem::Group;
        item.id = g.group_id();
        item.name = QString::fromStdString(g.group_name());
        item.group_id = g.group_id();
        item.owner_id = g.owner_id();
        item.role = QString::fromStdString(g.role());
        contacts_.append(item);
    }
    sortContacts();
    emit contactsUpdated();
}

void ClientState::emitGroupMembersFromRsp(uint64_t groupId,
                                          const google::protobuf::RepeatedPtrField<chatroom::GroupMember> &members) {// 响应中的成员列表直接更新
    QVector<GroupMemberItem> list;
    for (const auto &m : members) {
        GroupMemberItem item;
        item.user_id = m.user_id();
        item.username = QString::fromStdString(m.username());
        item.nickname = QString::fromStdString(m.nickname());
        item.role = QString::fromStdString(m.role());
        item.join_time = m.join_time();
        list.append(item);
    }
    emit groupMembersReceived(groupId, list);
}

void ClientState::handleGroupMembersResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.query_group_members_rsp();
    if (!rsp.success()) return;

    QVector<GroupMemberItem> members;
    for (const auto &m : rsp.members()) {
        GroupMemberItem item;
        item.user_id = m.user_id();
        item.username = QString::fromStdString(m.username());
        item.nickname = QString::fromStdString(m.nickname());
        item.role = QString::fromStdString(m.role());
        item.join_time = m.join_time();
        members.append(item);
    }
    emit groupMembersReceived(current_group_id_, members);
}

void ClientState::handleHistoryResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.get_history_rsp();
    if (!rsp.success()) return;

    // 防止切换聊天后，旧请求的响应污染当前聊天
    if (current_chat_type_ == ChatType::Private && msg.target_id() != current_target_id_) return;
    if (current_chat_type_ == ChatType::Group && msg.group_id() != current_group_id_) return;

    bool isGroup = (current_chat_type_ == ChatType::Group);

    // 服务端历史为最新的在前，逆序构建为旧 -> 新
    QVector<MessageItem> serverItems;
    for (int i = rsp.messages_size() - 1; i >= 0; --i) {
        const auto &m = rsp.messages(i);
        MessageItem item;
        item.message_id = m.message_id();
        item.sender_id = m.sender_id();
        item.sender_name = QString::fromStdString(m.sender_name());
        item.content = QString::fromStdString(m.content());
        item.timestamp = m.timestamp();
        item.is_self = (m.sender_id() == user_id_);
        item.status = MessageItem::Sent;
        item.msg_type = MessageItem::Text;
        serverItems.append(item);
    }

    // 本地乐观消息（message_id=0）被服务端确认后，用真实版本替换
    for (const auto &si : serverItems) {
        for (auto &lm : current_messages_) {
            if (lm.message_id == 0 && lm.sender_id == si.sender_id &&
                lm.content == si.content &&
                std::abs(static_cast<int64_t>(lm.timestamp) - static_cast<int64_t>(si.timestamp)) <= 300) {
                lm.message_id = si.message_id;
                lm.status = MessageItem::Sent;
                break;
            }
        }
    }

    // 合并去重（仅新增的入库）
    QSet<uint64_t> existing;
    for (const auto &m : current_messages_) {
        if (m.message_id != 0) existing.insert(m.message_id);
    }
    QVector<MessageItem> history;
    for (const auto &si : serverItems) {
        if (existing.contains(si.message_id)) continue;
        history.append(si);
        existing.insert(si.message_id);
        MessageStore::instance()->storeMessage(si, isGroup,
                                               current_target_id_, current_group_id_);
    }

    if (!history.isEmpty()) {
        // 新历史全部早于现有第一条（常规场景：上滑加载/进入聊天拉旧记录）→ 前置插入
        bool allBefore = true;
        if (!current_messages_.isEmpty()) {
            const auto &newest = history.last();  // history 已按时间升序
            if (newest.timestamp > current_messages_.first().timestamp) allBefore = false;
        }
        if (allBefore) {
            current_messages_ = history + current_messages_;
            emit messagesHistoryPrepended(history.size());
        } else {
            // 混合场景（含其他设备发的新消息）：全量按时间合并排序
            current_messages_ += history;
            std::sort(current_messages_.begin(), current_messages_.end(),
                      [](const MessageItem &a, const MessageItem &b) {
                          if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
                          return a.message_id < b.message_id;
                      });
        }
    }

    emit messagesUpdated();
}

void ClientState::handleChatResponse(const chatroom::ChatMessage &msg, bool isPrivate) {
    const auto &rsp = isPrivate ? msg.private_chat_rsp() : msg.group_chat_rsp();
    QString senderName = QString::fromStdString(rsp.sender_name());
    QString content = QString::fromStdString(rsp.content());

    // update last message in contacts if viewing appropriate chat
    if (isPrivate) {
        if (current_chat_type_ == ChatType::Private && current_target_id_ == msg.sender_id()) {
            MessageItem item;
            item.sender_id = msg.sender_id();
            item.sender_name = senderName;
            item.content = content;
            item.timestamp = msg.timestamp();
            item.is_self = false;
            item.status = MessageItem::Sent;
            item.msg_type = MessageItem::Text;
            current_messages_.append(item);
            MessageStore::instance()->storeMessage(item, false, current_target_id_, 0);
            emit messagesUpdated();
        }
        emit incomingMessage(msg.sender_id(), senderName, content);
    } else {
        if (current_chat_type_ == ChatType::Group && current_group_id_ == msg.group_id()) {
            MessageItem item;
            item.sender_id = msg.sender_id();
            item.sender_name = senderName;
            item.content = content;
            item.timestamp = msg.timestamp();
            item.is_self = false;
            item.status = MessageItem::Sent;
            item.msg_type = MessageItem::Text;
            current_messages_.append(item);
            MessageStore::instance()->storeMessage(item, true, 0, current_group_id_);
            emit messagesUpdated();
        }
        emit groupMessageReceived(msg.group_id(), msg.sender_id(), senderName, content);
    }
}

void ClientState::handleFileSendResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.file_send_rsp();
    emit operationResult(rsp.success(), QString::fromStdString(rsp.error_message()));
}

void ClientState::handleFileTransferNotify(const chatroom::ChatMessage &msg) {
    const auto &notify = msg.file_transfer_notify();
    emit fileTransferNotify(notify.transfer_id(), notify.sender_id(),
                            QString::fromStdString(notify.sender_name()),
                            QString::fromStdString(notify.file_name()),
                            notify.file_size());
}

void ClientState::sortContacts() {
    std::sort(contacts_.begin(), contacts_.end(), [](const ContactItem &a, const ContactItem &b) {
        if (a.last_chat_time != b.last_chat_time)
            return a.last_chat_time > b.last_chat_time;
        if (a.type != b.type)
            return a.type < b.type; // friends before groups if same time
        return a.add_time > b.add_time;
    });
}
