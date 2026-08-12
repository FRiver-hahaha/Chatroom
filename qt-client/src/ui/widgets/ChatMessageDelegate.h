#pragma once

#include <QStyledItemDelegate>
#include <QHash>
#include <QPixmap>
#include <QString>

// 消息气泡委托：每条消息渲染为一个 ChatBubble 组件
// 他人消息靠左、自己的消息靠右（类似微信/QQ）
class ChatMessageDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ChatMessageDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    // 行号 -> (内容指纹, 已渲染气泡位图)
    struct CacheEntry {
        QString fingerprint;
        QPixmap pixmap;
    };
    mutable QHash<int, CacheEntry> cache_;
};
