#include "MessageStore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QStandardPaths>
#include <QVariant>

MessageStore *MessageStore::instance_ = nullptr;

MessageStore::MessageStore(QObject *parent)
    : QObject(parent)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QString dbPath = dir + "/chatroom.db";

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "chatroom_local");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        qWarning("MessageStore: 无法打开本地数据库 %s", qPrintable(db.lastError().text()));
        return;
    }
    ensureTable();
    ready_ = true;
}

MessageStore *MessageStore::instance() {
    if (!instance_) instance_ = new MessageStore();
    return instance_;
}

void MessageStore::ensureTable() {
    QSqlQuery q(QSqlDatabase::database("chatroom_local"));
    q.exec("CREATE TABLE IF NOT EXISTS messages ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "chat_type INTEGER NOT NULL,"
           "peer_id INTEGER NOT NULL DEFAULT 0,"
           "group_id INTEGER NOT NULL DEFAULT 0,"
           "server_id INTEGER,"
           "sender_id INTEGER NOT NULL,"
           "sender_name TEXT NOT NULL DEFAULT '',"
           "content TEXT NOT NULL DEFAULT '',"
           "timestamp INTEGER NOT NULL DEFAULT 0,"
           "is_self INTEGER NOT NULL DEFAULT 0,"
           "msg_type INTEGER NOT NULL DEFAULT 0,"
           "file_name TEXT NOT NULL DEFAULT '',"
           "file_path TEXT NOT NULL DEFAULT '')");
    // server_id 为 NULL 表示本地乐观消息（未确认），多次发送不冲突；
    // 非 NULL 的服务端消息按 server_id 去重
    q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_server "
           "ON messages(chat_type, peer_id, group_id, server_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_chat "
           "ON messages(chat_type, peer_id, group_id, timestamp)");
}

void MessageStore::storeMessage(const MessageItem &msg, bool isGroup,
                                uint64_t peerId, uint64_t groupId) {
    if (!ready_) return;
    QSqlQuery q(QSqlDatabase::database("chatroom_local"));
    q.prepare("INSERT OR IGNORE INTO messages (chat_type, peer_id, group_id, server_id,"
              " sender_id, sender_name, content, timestamp, is_self, msg_type, file_name, file_path)"
              " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(isGroup ? 1 : 0);
    q.addBindValue(static_cast<qulonglong>(peerId));
    q.addBindValue(static_cast<qulonglong>(groupId));
    if (msg.message_id != 0)
        q.addBindValue(static_cast<qulonglong>(msg.message_id));
    else
        q.addBindValue(QVariant(QVariant::LongLong));  // NULL
    q.addBindValue(static_cast<qulonglong>(msg.sender_id));
    q.addBindValue(msg.sender_name);
    q.addBindValue(msg.content);
    q.addBindValue(static_cast<qulonglong>(msg.timestamp));
    q.addBindValue(msg.is_self ? 1 : 0);
    q.addBindValue(static_cast<int>(msg.msg_type));
    q.addBindValue(msg.file_name);
    q.addBindValue(msg.file_path);
    q.exec();
}

QVector<MessageItem> MessageStore::loadHistory(bool isGroup, uint64_t peerId,
                                               uint64_t groupId, int limit) {
    QVector<MessageItem> result;
    if (!ready_) return result;

    QSqlQuery q(QSqlDatabase::database("chatroom_local"));
    q.prepare("SELECT server_id, sender_id, sender_name, content, timestamp, is_self,"
              " msg_type, file_name, file_path FROM messages"
              " WHERE chat_type=? AND peer_id=? AND group_id=?"
              " ORDER BY timestamp ASC, id ASC LIMIT ?");
    q.addBindValue(isGroup ? 1 : 0);
    q.addBindValue(static_cast<qulonglong>(peerId));
    q.addBindValue(static_cast<qulonglong>(groupId));
    q.addBindValue(limit);
    if (!q.exec()) return result;

    while (q.next()) {
        MessageItem item;
        item.message_id = q.value(0).toULongLong();
        item.sender_id = q.value(1).toULongLong();
        item.sender_name = q.value(2).toString();
        item.content = q.value(3).toString();
        item.timestamp = q.value(4).toULongLong();
        item.is_self = q.value(5).toBool();
        item.msg_type = static_cast<MessageItem::MsgType>(q.value(6).toInt());
        item.file_name = q.value(7).toString();
        item.file_path = q.value(8).toString();
        item.status = MessageItem::Sent;
        result.append(item);
    }
    return result;
}

void MessageStore::clearChat(bool isGroup, uint64_t peerId, uint64_t groupId) {
    if (!ready_) return;
    QSqlQuery q(QSqlDatabase::database("chatroom_local"));
    q.prepare("DELETE FROM messages WHERE chat_type=? AND peer_id=? AND group_id=?");
    q.addBindValue(isGroup ? 1 : 0);
    q.addBindValue(static_cast<qulonglong>(peerId));
    q.addBindValue(static_cast<qulonglong>(groupId));
    q.exec();
}