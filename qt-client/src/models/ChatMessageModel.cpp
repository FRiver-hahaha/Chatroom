#include "ChatMessageModel.h"

ChatMessageModel::ChatMessageModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ChatMessageModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return static_cast<int>(messages_.size());
}

QVariant ChatMessageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= messages_.size())
        return {};

    const auto &msg = messages_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case ContentRole:
        return msg.content;
    case SenderNameRole:
        return msg.sender_name;
    case TimestampRole:
        return QVariant::fromValue(msg.timestamp);
    case IsSelfRole:
        return msg.is_self;
    case StatusRole:
        return static_cast<int>(msg.status);
    case MsgTypeRole:
        return static_cast<int>(msg.msg_type);
    case FileNameRole:
        return msg.file_name;
    case FilePathRole:
        return msg.file_path;
    case MessageItemRole:
        return QVariant::fromValue(msg);
    default:
        return {};
    }
}

void ChatMessageModel::setMessages(const QVector<MessageItem> &messages) {
    beginResetModel();
    messages_ = messages;
    endResetModel();
}

void ChatMessageModel::appendMessage(const MessageItem &msg) {
    beginInsertRows(QModelIndex(), messages_.size(), messages_.size());
    messages_.append(msg);
    endInsertRows();
}

void ChatMessageModel::updateLastStatus(MessageItem::Status status) {
    if (messages_.isEmpty()) return;
    int last = messages_.size() - 1;
    messages_[last].status = status;
    emit dataChanged(index(last), index(last));
}
