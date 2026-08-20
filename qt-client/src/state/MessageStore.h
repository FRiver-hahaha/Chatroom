#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include "state/ClientState.h"

class QSqlDatabase;

// 本地聊天记录存储（SQLite）
// 客户端将拉取/收发过的消息缓存到本地数据库，离线也能查看历史记录
class MessageStore : public QObject {
    Q_OBJECT
public:
    static MessageStore *instance();

    bool isReady() const { return ready_; }

    // 按会话保存一条消息（私聊 peer_id 或群聊 group_id 二选一）
    void storeMessage(const MessageItem &msg, bool isGroup, uint64_t peerId, uint64_t groupId);

    // 本地乐观消息（server_id 为空）被服务端确认后，用真实 server_id 替换对应行，避免重进会话时重复显示
    void confirmMessage(const MessageItem &msg, bool isGroup, uint64_t peerId, uint64_t groupId);

    // 加载某个会话最近 limit 条本地消息（时间升序）
    QVector<MessageItem> loadHistory(bool isGroup, uint64_t peerId, uint64_t groupId, int limit = 200);

    // 删除某个会话的全部本地记录（删除好友/退出群组时使用）
    void clearChat(bool isGroup, uint64_t peerId, uint64_t groupId);

    // 清空全部本地记录（退出登录/注销账号时使用）
    void clearAll();

private:
    explicit MessageStore(QObject *parent = nullptr);

    void ensureTable();

    static MessageStore *instance_;
    bool ready_ = false;
};
