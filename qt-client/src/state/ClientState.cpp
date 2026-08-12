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

void ClientState::registerUser(const QString &username, const QString &password, const QString &nickname) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REGISTER_RSP),
        [this](const chatroom::ChatMessage &msg) { handleRegisterResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::REGISTER_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_register_req();
        body->set_username(username.toStdString());
        body->set_password(password.toStdString());
        body->set_nickname(nickname.toStdString());
    });
    pending_nickname_ = nickname;
    login_timeout_timer_->start(LoginTimeoutMs);
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

// ===== friends =====

void ClientState::queryFriends() {
    storePendingCallback(static_cast<uint32_t>(MessageType::QUERY_FRIEND_RSP),
        [this](const chatroom::ChatMessage &msg) { handleFriendListResponse(msg); });
    buildAndSend(static_cast<uint32_t>(MessageType::QUERY_FRIEND_REQ), nullptr);
}

void ClientState::addFriend(uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::ADD_FRIEND_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.add_friend_rsp().success(),
                                 QString::fromStdString(msg.add_friend_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::ADD_FRIEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetUserId);
        m.mutable_add_friend_req()->set_target_user_id(targetUserId);
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
            emit operationResult(msg.query_blocked_rsp().success(), "");
        });
    buildAndSend(static_cast<uint32_t>(MessageType::QUERY_BLOCKED_REQ), nullptr);
}

// ===== groups =====

void ClientState::createGroup(const QString &name, const QString &desc, bool isPublic) {
    storePendingCallback(static_cast<uint32_t>(MessageType::CREATE_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
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
            emit operationResult(msg.join_group_rsp().success(),
                                 QString::fromStdString(msg.join_group_rsp().error_message()));
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
            emit operationResult(msg.add_group_admin_rsp().success(),
                                 QString::fromStdString(msg.add_group_admin_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::ADD_GROUP_ADMIN_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_add_group_admin_req()->set_group_id(groupId);
        m.mutable_add_group_admin_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::removeGroupAdmin(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REMOVE_GROUP_ADMIN_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.remove_group_admin_rsp().success(),
                                 QString::fromStdString(msg.remove_group_admin_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::REMOVE_GROUP_ADMIN_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_remove_group_admin_req()->set_group_id(groupId);
        m.mutable_remove_group_admin_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::approveJoinGroup(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::APPROVE_JOIN_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.approve_join_group_rsp().success(),
                                 QString::fromStdString(msg.approve_join_group_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::APPROVE_JOIN_GROUP_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_approve_join_group_req()->set_group_id(groupId);
        m.mutable_approve_join_group_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::removeGroupMember(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REMOVE_GROUP_MEMBER_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.remove_group_member_rsp().success(),
                                 QString::fromStdString(msg.remove_group_member_rsp().error_message()));
        });
    buildAndSend(static_cast<uint32_t>(MessageType::REMOVE_GROUP_MEMBER_REQ), [&](chatroom::ChatMessage &m) {
        m.mutable_remove_group_member_req()->set_group_id(groupId);
        m.mutable_remove_group_member_req()->set_target_user_id(targetUserId);
    });
}

void ClientState::rejectJoinGroup(uint64_t groupId, uint64_t targetUserId) {
    storePendingCallback(static_cast<uint32_t>(MessageType::REJECT_JOIN_GROUP_RSP),
        [this](const chatroom::ChatMessage &msg) {
            emit operationResult(msg.reject_join_group_rsp().success(),
                                 QString::fromStdString(msg.reject_join_group_rsp().error_message()));
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
    QByteArray fileHashRaw = sha256Hex(fileData).toUtf8();

    storePendingCallback(static_cast<uint32_t>(MessageType::FILE_SEND_RSP),
        [this](const chatroom::ChatMessage &msg) { handleFileSendResponse(msg); });

    buildAndSend(static_cast<uint32_t>(MessageType::FILE_SEND_REQ), [&](chatroom::ChatMessage &m) {
        m.set_target_id(targetId);
        auto *body = m.mutable_file_send_req();
        body->set_file_name(info.fileName().toStdString());
        body->set_file_size(fileSize);
        body->set_total_chunks(totalChunks);
        body->set_file_hash(fileHashRaw.toStdString());
    });
}

void ClientState::acceptFileTransfer(uint64_t transferId, bool accept) {
    buildAndSend(static_cast<uint32_t>(MessageType::FILE_TRANSFER_ACCEPT_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_file_transfer_accept_req();
        body->set_transfer_id(transferId);
        body->set_accept(accept);
    });
}

void ClientState::receiveFileChunks(uint64_t transferId, const QString &savePath) {
    storePendingCallback(static_cast<uint32_t>(MessageType::FILE_RECEIVE_CHUNK_RSP),
        [this](const chatroom::ChatMessage &) {
            emit operationResult(true, "File received");
        });
    buildAndSend(static_cast<uint32_t>(MessageType::FILE_RECEIVE_CHUNK_REQ), [&](chatroom::ChatMessage &m) {
        auto *body = m.mutable_file_receive_chunk_req();
        body->set_transfer_id(transferId);
        body->set_chunk_seq(0);
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

    // pending callback match
    auto it = pending_callbacks_.find(msg.type());
    if (it != pending_callbacks_.end()) {
        auto cb = it.value();
        pending_callbacks_.erase(it);
        cb(msg);
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
        contacts_.append(item);
    }
    sortContacts();
    emit contactsUpdated();
}

void ClientState::handleGroupMembersResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.query_group_members_rsp();
    if (!rsp.success()) return;

    QStringList names;
    for (const auto &m : rsp.members()) {
        QString name = QString::fromStdString(m.nickname().empty() ? m.username() : m.nickname());
        QString role = QString::fromStdString(m.role());
        names.append(name + (role.isEmpty() ? "" : " [" + role + "]"));
    }
    emit groupMembersReceived(current_group_id_, names);
}

void ClientState::handleHistoryResponse(const chatroom::ChatMessage &msg) {
    const auto &rsp = msg.get_history_rsp();
    if (!rsp.success()) return;

    // 服务端历史为最新的在前；与本地已有记录去重后前置插入
    QSet<uint64_t> existing;
    for (const auto &m : current_messages_) {
        if (m.message_id != 0) existing.insert(m.message_id);
    }

    bool isGroup = (current_chat_type_ == ChatType::Group);
    QVector<MessageItem> history;
    for (int i = rsp.messages_size() - 1; i >= 0; --i) {
        const auto &m = rsp.messages(i);
        if (existing.contains(m.message_id())) continue;

        QString content = QString::fromStdString(m.content());
        bool isSelf = (m.sender_id() == user_id_);

        // 本地乐观消息（message_id=0，发送时缓存）被服务端确认后，用服务端版本替换
        for (int j = 0; j < current_messages_.size(); ++j) {
            const auto &lm = current_messages_[j];
            if (lm.message_id == 0 && lm.sender_id == m.sender_id() && lm.is_self == isSelf &&
                lm.content == content &&
                std::abs(static_cast<int64_t>(lm.timestamp) - static_cast<int64_t>(m.timestamp())) <= 300) {
                current_messages_.removeAt(j);
                break;
            }
        }

        MessageItem item;
        item.message_id = m.message_id();
        item.sender_id = m.sender_id();
        item.sender_name = QString::fromStdString(m.sender_name());
        item.content = content;
        item.timestamp = m.timestamp();
        item.is_self = isSelf;
        item.status = MessageItem::Sent;
        item.msg_type = MessageItem::Text;
        history.append(item);
        MessageStore::instance()->storeMessage(item, isGroup,
                                               current_target_id_, current_group_id_);
    }

    if (!history.isEmpty()) {
        current_messages_ = history + current_messages_;
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
