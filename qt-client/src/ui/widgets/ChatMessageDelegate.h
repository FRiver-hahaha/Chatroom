#pragma once

#include <QStyledItemDelegate>
#include <QHash>
#include <QPixmap>
#include <QString>

// 消息气泡委托：直接绘制气泡，不实例化 QWidget，保证长文本与大量消息的流畅性
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
    // 内容指纹 -> 已渲染气泡位图（按消息内容缓存，行号变化不影响正确性）
    struct CacheEntry {
        QString fingerprint;
        QPixmap pixmap;
    };
    mutable QHash<QString, CacheEntry> cache_;
};