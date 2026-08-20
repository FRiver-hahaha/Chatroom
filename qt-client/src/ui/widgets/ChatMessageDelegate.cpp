#include "ChatMessageDelegate.h"
#include "models/ChatMessageModel.h"
#include <QPainter>
#include <QTextDocument>
#include <QTextOption>
#include <QFontMetrics>
#include <QDateTime>

namespace {

constexpr int kBubbleMaxWidth = 380;  // 气泡最大宽度
constexpr int kSideMargin = 8;        // 视图左右边距
constexpr int kRowMargin = 6;         // 行上下间距
constexpr int kHPadding = 16;         // 气泡内左右内边距
constexpr int kVPadding = 10;         // 气泡内上下内边距
constexpr int kTimeHeight = 14;       // 时间行高
constexpr int kNameHeight = 14;       // 发送者名字行高
constexpr int kMaxCacheEntries = 300; // 位图缓存上限，防止长会话内存膨胀

QString messageFingerprint(const MessageItem &msg, const QSize &size) {
    return QString::number(msg.message_id) + "|" + QString::number(msg.sender_id) + "|"
         + msg.sender_name + "|" + msg.content + "|" + QString::number(msg.timestamp)
         + "|" + QString::number(msg.is_self) + "|" + QString::number(msg.status)
         + "|" + QString::number(msg.msg_type) + "|" + msg.file_name + "|" + msg.file_path
         + "|" + QString::number(size.width()) + "x" + QString::number(size.height());
}

// 计算消息正文的高度（QTextDocument 自动换行，正确处理长文本）
int contentHeight(const QString &text, int textWidth) {
    QTextDocument doc;
    QFont f;
    f.setPointSizeF(10.5);
    doc.setDefaultFont(f);
    doc.setDocumentMargin(0);
    doc.setTextWidth(textWidth);
    doc.setPlainText(text);
    return static_cast<int>(doc.size().height());
}

} // namespace

ChatMessageDelegate::ChatMessageDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

QSize ChatMessageDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    if (!index.isValid()) return QStyledItemDelegate::sizeHint(option, index);
    const auto msg = index.data(ChatMessageModel::MessageItemRole)
                         .value<MessageItem>();

    int avail = (option.rect.width() > 0 ? option.rect.width() : 500)
                - 2 * kSideMargin;
    // 时间文本宽度（含年月日），窄气泡也需容纳，避免日期溢出气泡
    QFont timeFont;
    timeFont.setPointSizeF(8);
    QString timeStr = QDateTime::fromSecsSinceEpoch(msg.timestamp).toString("yyyy-MM-dd HH:mm");
    int timeW = QFontMetrics(timeFont).horizontalAdvance(timeStr);
    int bubbleW = qMax(qMin(avail - 16, kBubbleMaxWidth), timeW + kHPadding);  // 为另一侧留出空间
    int textW = bubbleW - kHPadding;

    int h = kVPadding * 2 + kTimeHeight + contentHeight(msg.content, textW);
    if (!msg.is_self) h += kNameHeight + 4;

    return QSize(avail, h + 2 * kRowMargin);
}

void ChatMessageDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    if (!index.isValid()) return;
    const auto msg = index.data(ChatMessageModel::MessageItemRole)
                         .value<MessageItem>();

    QString fp = messageFingerprint(msg, option.rect.size());
    auto it = cache_.find(fp);
    if (it == cache_.end()) {
        if (cache_.size() >= kMaxCacheEntries) cache_.clear();

        // 在内存位图中直接绘制气泡（不实例化 QWidget，性能好且不会错乱）
        QPixmap pm(option.rect.size());
        pm.fill(Qt::transparent);
        QPainter pmPainter(&pm);
        pmPainter.setRenderHint(QPainter::Antialiasing);

        int avail = option.rect.width() - 2 * kSideMargin;
        // 时间文本宽度（含年月日），窄气泡也需容纳，避免日期溢出气泡
        QFont timeFont;
        timeFont.setPointSizeF(8);
        QString timeStr = QDateTime::fromSecsSinceEpoch(msg.timestamp).toString("yyyy-MM-dd HH:mm");
        int timeW = QFontMetrics(timeFont).horizontalAdvance(timeStr);
        int bubbleW = qMax(qMin(avail - 16, kBubbleMaxWidth), timeW + kHPadding);
        int textW = bubbleW - kHPadding;

        // 气泡外框（右侧消息贴右、左侧消息贴左）
        QRect bubbleRect(0, 0, bubbleW, option.rect.height() - 2 * kRowMargin);
        if (msg.is_self) bubbleRect.moveRight(option.rect.width() - kSideMargin);
        else bubbleRect.moveLeft(kSideMargin);

        // 气泡底色
        pmPainter.setPen(Qt::NoPen);
        pmPainter.setBrush(msg.is_self ? QColor("#95ec69") : QColor("#ffffff"));
        pmPainter.drawRoundedRect(bubbleRect, 8, 8);

        // 发送者名字（他人消息）
        int textTop = bubbleRect.top() + kVPadding;
        if (!msg.is_self) {
            pmPainter.setPen(QColor("#888888"));
            QFont nameFont;
            nameFont.setPointSizeF(8.5);
            pmPainter.setFont(nameFont);
            pmPainter.drawText(QPoint(bubbleRect.left() + kHPadding / 2, textTop + kNameHeight - 3),
                               msg.sender_name);
            textTop += kNameHeight + 4;
        }

        // 消息正文（自动换行）
        QTextDocument doc;
        QFont f;
        f.setPointSizeF(10.5);
        doc.setDefaultFont(f);
        doc.setDocumentMargin(0);
        doc.setTextWidth(textW);
        doc.setPlainText(msg.content);

        pmPainter.translate(bubbleRect.left() + kHPadding / 2, textTop);
        pmPainter.setPen(QColor("#000000"));
        doc.drawContents(&pmPainter);
        pmPainter.resetTransform();

        // 时间（右下角，含年月日）
        pmPainter.setPen(QColor("#aaaaaa"));
        pmPainter.setFont(timeFont);
        pmPainter.drawText(QPoint(bubbleRect.right() - kHPadding / 2
                                      - pmPainter.fontMetrics().horizontalAdvance(timeStr),
                                  bubbleRect.bottom() - 4),
                           timeStr);

        it = cache_.insert(fp, {fp, pm});
    }

    painter->save();
    painter->drawPixmap(option.rect.topLeft(), it->pixmap);
    painter->restore();
}