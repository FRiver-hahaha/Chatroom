#include "ChatBubble.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDateTime>

ChatBubble::ChatBubble(const MessageItem &msg, QWidget *parent)
    : QWidget(parent)
{
    setupUi(msg);
}

void ChatBubble::setupUi(const MessageItem &msg) {
    auto *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(8, 2, 8, 2);

    auto *bubble = new QWidget();
    bubble->setMaximumWidth(400);
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(10, 6, 10, 6);

    // sender name (for group chats or if not self)
    if (!msg.is_self) {
        auto *nameLabel = new QLabel(msg.sender_name);
        nameLabel->setStyleSheet("font-size: 11px; color: #888;");
        bubbleLayout->addWidget(nameLabel);
    }

    // content
    if (msg.msg_type == MessageItem::File) {
        auto *fileLabel = new QLabel(msg.file_name);
        fileLabel->setWordWrap(true);
        fileLabel->setStyleSheet("font-size: 13px; font-weight: bold;");
        bubbleLayout->addWidget(fileLabel);

        auto *pathLabel = new QLabel(msg.file_path);
        pathLabel->setWordWrap(true);
        pathLabel->setStyleSheet("font-size: 10px; color: #666;");
        bubbleLayout->addWidget(pathLabel);
    } else {
        auto *contentLabel = new QLabel(msg.content);
        contentLabel->setWordWrap(true);
        contentLabel->setMaximumWidth(370);
        contentLabel->setStyleSheet("font-size: 14px;");
        bubbleLayout->addWidget(contentLabel);
    }

    // timestamp
    auto *timeLabel = new QLabel(QDateTime::fromSecsSinceEpoch(msg.timestamp).toString("HH:mm"));
    timeLabel->setStyleSheet("font-size: 10px; color: #aaa;");
    timeLabel->setAlignment(Qt::AlignRight);
    bubbleLayout->addWidget(timeLabel);

    // bubble styling
    if (msg.is_self) {
        bubble->setStyleSheet(
            "QWidget { background-color: #95ec69; border-radius: 8px; }");
    } else {
        bubble->setStyleSheet(
            "QWidget { background-color: #ffffff; border-radius: 8px; }");
    }

    // failed indicator
    if (msg.status == MessageItem::Failed) {
        auto *failIcon = new QLabel("!");
        failIcon->setStyleSheet(
            "color: white; background-color: red; border-radius: 10px; "
            "font-weight: bold; font-size: 14px; padding: 2px 8px;");
        failIcon->setFixedSize(20, 20);
        failIcon->setAlignment(Qt::AlignCenter);

        if (msg.is_self) {
            outerLayout->addWidget(failIcon);
            outerLayout->addStretch();
            outerLayout->addWidget(bubble);
        } else {
            outerLayout->addWidget(bubble);
            outerLayout->addStretch();
            outerLayout->addWidget(failIcon);
        }
    } else if (msg.is_self) {
        outerLayout->addStretch();
        outerLayout->addWidget(bubble);
    } else {
        outerLayout->addWidget(bubble);
        outerLayout->addStretch();
    }

    // sending indicator
    if (msg.status == MessageItem::Sending) {
        setStyleSheet("ChatBubble { opacity: 0.6; }");
    }
}
