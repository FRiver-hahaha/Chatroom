#include "ChatMessageDelegate.h"
#include "ChatBubble.h"
#include "models/ChatMessageModel.h"
#include <QPainter>

ChatMessageDelegate::ChatMessageDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

QSize ChatMessageDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    if (!index.isValid()) return QStyledItemDelegate::sizeHint(option, index);
    const auto msg = index.data(ChatMessageModel::MessageItemRole)
                         .value<MessageItem>();
    if (msg.content.isEmpty() && msg.file_name.isEmpty())
        return QStyledItemDelegate::sizeHint(option, index);

    int width = option.rect.width() > 0 ? option.rect.width() : 500;
    ChatBubble bubble(msg);
    bubble.setFixedWidth(width);
    bubble.adjustSize();
    return bubble.sizeHint();
}

void ChatMessageDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    if (!index.isValid()) return;
    const auto msg = index.data(ChatMessageModel::MessageItemRole)
                         .value<MessageItem>();

    // 内容指纹：内容变化时缓存自动失效
    QString fp = QString::number(msg.sender_id) + "|" + msg.sender_name + "|"
                 + msg.content + "|" + QString::number(msg.timestamp) + "|"
                 + QString::number(msg.is_self) + "|" + QString::number(msg.status) + "|"
                 + QString::number(msg.msg_type) + "|" + msg.file_name + "|" + msg.file_path;

    auto &entry = cache_[index.row()];
    if (entry.fingerprint != fp || entry.pixmap.isNull() ||
        entry.pixmap.size() != option.rect.size()) {
        ChatBubble bubble(msg);
        bubble.setFixedWidth(option.rect.width());
        bubble.adjustSize();

        qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;
        QSize pmSize(option.rect.size());
        entry.pixmap = QPixmap(pmSize * dpr);
        entry.pixmap.setDevicePixelRatio(dpr);
        entry.pixmap.fill(Qt::transparent);

        QPainter pmPainter(&entry.pixmap);
        bubble.resize(option.rect.size());
        bubble.render(&pmPainter);
        entry.fingerprint = fp;
    }

    painter->save();
    painter->drawPixmap(option.rect.topLeft(), entry.pixmap);
    painter->restore();
}
