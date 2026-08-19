#pragma once

#include <QObject>
#include <QVector>
#include <QMap>
#include <QString>
#include <functional>
#include "chatroom.pb.h"

class ProtocolClient;
class QTimer;

enum class ChatType { Private, Group };

struct MessageItem {
    uint64_t message_id = 0;
    uint64_t sender_id = 0;
    QString sender_name;
    QString content;
    uint64_t timestamp = 0;
    bool is_self = false;
    enum Status { Sending, Sent, Failed } status = Sent;
    enum MsgType { Text, File } msg_type = Text;
    QString file_name;
    QString file_path;
    uint64_t transfer_id = 0;
};

struct ContactItem {
    enum Type { Friend, Group };
    Type type;
    uint64_t id;
    QString name;
    bool is_online = false;
    uint64_t last_chat_time = 0;
    uint64_t add_time = 0;
    uint64_t streak_days = 0; // 续火花天数（仅好友）
    QString role; // only for groups
    uint64_t group_id = 0; // only for groups
    uint64_t owner_id = 0; // only for groups
};

Q_DECLARE_METATYPE(MessageItem)
Q_DECLARE_METATYPE(ContactItem)

class ClientState : public QObject {
    Q_OBJECT
public:
    static ClientState *instance();

    void setProtocolClient(ProtocolClient *client);

    // account
    void login(const QString &username, const QString &password);
    void registerUser(const QString &username, const QString &password,
                      const QString &nickname, const QString &email, const QString &verifyCode);
    void sendVerifyCode(const QString &channel, const QString &target, const QString &scene);
    void resetPassword(const QString &channel, const QString &target,
                       const QString &verifyCode, const QString &newPassword);
    void logout();
    void deleteAccount(const QString &password);
    void updateNickname(const QString &nickname);

    // friends
    void queryFriends();
    void addFriend(uint64_t targetUserId, const QString &targetEmail = QString());
    void deleteFriend(uint64_t targetUserId);
    void blockFriend(uint64_t targetUserId);
    void unblockFriend(uint64_t targetUserId);
    void queryBlockedUsers();

    // groups
    void createGroup(const QString &name, const QString &desc, bool isPublic);
    void joinGroup(uint64_t groupId);
    void quitGroup(uint64_t groupId);
    void dismissGroup(uint64_t groupId);
    void queryGroupList();
    void queryGroupMembers(uint64_t groupId);
    void addGroupAdmin(uint64_t groupId, uint64_t targetUserId);
    void removeGroupAdmin(uint64_t groupId, uint64_t targetUserId);
    void approveJoinGroup(uint64_t groupId, uint64_t targetUserId);
    void removeGroupMember(uint64_t groupId, uint64_t targetUserId);
    void rejectJoinGroup(uint64_t groupId, uint64_t targetUserId);

    // chat
    void sendPrivateChat(uint64_t targetId, const QString &text);
    void sendGroupChat(uint64_t groupId, const QString &text);
    void getHistory(uint64_t targetId, uint64_t groupId, int limit = 50);

    // login timeout (5s)
    static constexpr int LoginTimeoutMs = 5000;

    // file
    void sendFileRequest(uint64_t targetId, const QString &filePath);
    void acceptFileTransfer(uint64_t transferId, bool accept);
    void receiveFileChunks(uint64_t transferId, const QString &saveDir);

    // state accessors
    bool isLoggedIn() const { return !token_.isEmpty(); }
    uint64_t userId() const { return user_id_; }
    QString username() const { return username_; }
    QString nickname() const { return nickname_; }
    QVector<ContactItem> contacts() const { return contacts_; }
    QVector<MessageItem> currentChatMessages() const { return current_messages_; }
    ChatType currentChatType() const { return current_chat_type_; }
    uint64_t currentTargetId() const { return current_target_id_; }
    uint64_t currentGroupId() const { return current_group_id_; }

    void setCurrentChat(ChatType type, uint64_t targetId, uint64_t groupId = 0);
    ContactItem contactById(uint64_t id, ContactItem::Type type) const;

signals:
    void loginResult(bool success, const QString &errorMsg);
    void registerResult(bool success, const QString &errorMsg);
    void verifyCodeResult(bool success, const QString &errorMsg);
    void operationResult(bool success, const QString &errorMsg);
    void logoutDone();
    void deleteAccountResult(bool success, const QString &errorMsg);
    void passwordResetResult(bool success, const QString &errorMsg);

    void contactsUpdated();
    void currentChatChanged();
    void messagesUpdated();
    void incomingMessage(uint64_t fromId, const QString &senderName, const QString &content);
    void groupMessageReceived(uint64_t groupId, uint64_t senderId, const QString &senderName, const QString &content);
    void groupMembersReceived(uint64_t groupId, const QStringList &members);
    void blockedUsersReceived(const QVector<ContactItem> &users);
    void systemNotification(const QString &text);
    void friendOnlineChanged(uint64_t userId, bool online);

    void fileTransferNotify(uint64_t transferId, uint64_t senderId,
                            const QString &senderName, const QString &fileName,
                            uint64_t fileSize);

private slots:
    void onMessageReceived(const chatroom::ChatMessage &msg);
    void onNotification(const QString &text);
    void onConnected();
    void onDisconnected();
    void refreshContacts();

private:
    explicit ClientState(QObject *parent = nullptr);

    void buildAndSend(uint32_t type, const std::function<void(chatroom::ChatMessage &)> &setBody);
    void storePendingCallback(uint32_t rspType, std::function<void(const chatroom::ChatMessage &)> cb);
    void handleChatResponse(const chatroom::ChatMessage &msg, bool isPrivate);
    void handleLoginResponse(const chatroom::ChatMessage &msg);
    void handleRegisterResponse(const chatroom::ChatMessage &msg);
    void handleFriendListResponse(const chatroom::ChatMessage &msg);
    void handleGroupListResponse(const chatroom::ChatMessage &msg);
    void handleGroupMembersResponse(const chatroom::ChatMessage &msg);
    void handleHistoryResponse(const chatroom::ChatMessage &msg);
    void handleFileSendResponse(const chatroom::ChatMessage &msg);
    void handleFileTransferNotify(const chatroom::ChatMessage &msg);
    void uploadNextChunk(uint64_t transferId, const QString &fileName, uint64_t fileSize,
                         const QByteArray &fileData, uint32_t totalChunks, uint32_t nextSeq);
    void requestNextChunk(uint64_t transferId, const QString &savePath, const QString &fileName,
                          uint64_t fileSize, uint32_t totalChunks, uint32_t nextSeq);
    void sortContacts();
    void onLoginTimeout();
    void handleVerifyCodeResponse(const chatroom::ChatMessage &msg);

    ProtocolClient *client_ = nullptr;
    uint64_t user_id_ = 0;
    QString username_;
    QString nickname_;
    QString pending_nickname_;
    QString token_;

    QVector<ContactItem> contacts_;
    QVector<MessageItem> current_messages_;
    ChatType current_chat_type_ = ChatType::Private;
    uint64_t current_target_id_ = 0;
    uint64_t current_group_id_ = 0;

    QMap<uint32_t, std::function<void(const chatroom::ChatMessage &)>> pending_callbacks_;

    QTimer *refresh_timer_ = nullptr;
    QTimer *login_timeout_timer_ = nullptr;

    static ClientState *instance_;
};
