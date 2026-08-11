#pragma once

#include <QAbstractListModel>
#include <QVector>
#include "state/ClientState.h"

class ChatMessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        SenderNameRole = Qt::UserRole + 1,
        ContentRole,
        TimestampRole,
        IsSelfRole,
        StatusRole,
        MsgTypeRole,
        FileNameRole,
        FilePathRole
    };

    explicit ChatMessageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setMessages(const QVector<MessageItem> &messages);
    void appendMessage(const MessageItem &msg);
    void updateLastStatus(MessageItem::Status status);

private:
    QVector<MessageItem> messages_;
};
