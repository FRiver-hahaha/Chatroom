#include "ChatInputBar.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QLabel>

ChatInputBar::ChatInputBar(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    auto *bar = new QWidget();
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);

    auto *editBox = new QWidget();
    auto *editLayout = new QVBoxLayout(editBox);
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->setSpacing(0);

    text_edit_ = new QTextEdit();
    text_edit_->setPlaceholderText("输入消息...");
    text_edit_->setMaximumHeight(80);
    text_edit_->setMinimumHeight(36);
    editLayout->addWidget(text_edit_);

    char_count_label_ = new QLabel(QString::fromUtf8("0/5000 字"));
    char_count_label_->setAlignment(Qt::AlignRight);
    char_count_label_->setStyleSheet("font-size: 10px; color: #999; padding-right: 2px;");
    editLayout->addWidget(char_count_label_);

    barLayout->addWidget(editBox, 1);

    file_btn_ = new QPushButton("文件");
    file_btn_->setMinimumWidth(50);
    barLayout->addWidget(file_btn_);

    send_btn_ = new QPushButton("发送");
    send_btn_->setMinimumWidth(60);
    barLayout->addWidget(send_btn_);

    mainLayout->addWidget(bar);

    connect(send_btn_, &QPushButton::clicked, this, &ChatInputBar::onSendClicked);

    connect(text_edit_, &QTextEdit::textChanged, this, &ChatInputBar::updateCharCount);

    connect(file_btn_, &QPushButton::clicked, this, &ChatInputBar::fileUploadClicked);
}

void ChatInputBar::updateCharCount() {
    int len = text_edit_->toPlainText().trimmed().size();
    if (len > MaxTextLength) {
        char_count_label_->setStyleSheet(
            "font-size: 10px; color: red; padding-right: 2px;");
        char_count_label_->setText(
            QString::fromUtf8("%1/%2 字，已超出上限，无法发送")
                .arg(len).arg(MaxTextLength));
    } else {
        char_count_label_->setStyleSheet(
            "font-size: 10px; color: #999; padding-right: 2px;");
        char_count_label_->setText(
            QString::fromUtf8("%1/%2 字").arg(len).arg(MaxTextLength));
    }
}

void ChatInputBar::onSendClicked() {
    QString text = text_edit_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    if (text.size() > MaxTextLength) {
        updateCharCount();
        return;
    }
    emit sendClicked(text);
    text_edit_->clear();
}

void ChatInputBar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            onSendClicked();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}