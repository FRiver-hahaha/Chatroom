#include "ChatInputBar.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

ChatInputBar::ChatInputBar(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // normal mode
    auto *normalBar = new QWidget();
    auto *normalLayout = new QHBoxLayout(normalBar);
    normalLayout->setContentsMargins(0, 0, 0, 0);

    text_edit_ = new QTextEdit();
    text_edit_->setPlaceholderText("输入消息...");
    text_edit_->setMaximumHeight(80);
    text_edit_->setMinimumHeight(36);
    normalLayout->addWidget(text_edit_, 1);

    send_btn_ = new QPushButton("发送");
    send_btn_->setMinimumWidth(60);
    normalLayout->addWidget(send_btn_);

    file_btn_ = new QPushButton("文件");
    file_btn_->setMinimumWidth(50);
    normalLayout->addWidget(file_btn_);

    game_btn_ = new QPushButton("游戏");
    game_btn_->setMinimumWidth(50);
    normalLayout->addWidget(game_btn_);

    history_btn_ = new QPushButton("历史");
    history_btn_->setMinimumWidth(50);
    normalLayout->addWidget(history_btn_);

    mainLayout->addWidget(normalBar);

    // game mode bar
    auto *gameBar = new QWidget();
    auto *gameLayout = new QHBoxLayout(gameBar);
    gameLayout->setContentsMargins(0, 0, 0, 0);

    rock_btn_ = new QPushButton("石头");
    paper_btn_ = new QPushButton("剪刀");
    scissors_btn_ = new QPushButton("布");
    rock_btn_->setMinimumHeight(40);
    paper_btn_->setMinimumHeight(40);
    scissors_btn_->setMinimumHeight(40);
    gameLayout->addWidget(rock_btn_);
    gameLayout->addWidget(paper_btn_);
    gameLayout->addWidget(scissors_btn_);
    gameBar->hide();
    mainLayout->addWidget(gameBar);

    connect(send_btn_, &QPushButton::clicked, this, [this]() {
        QString text = text_edit_->toPlainText().trimmed();
        if (!text.isEmpty()) {
            emit sendClicked(text);
            text_edit_->clear();
        }
    });

    connect(text_edit_, &QTextEdit::textChanged, this, [this]() {
        // allow Enter to send, Shift+Enter for newline
    });

    connect(file_btn_, &QPushButton::clicked, this, &ChatInputBar::fileUploadClicked);
    connect(game_btn_, &QPushButton::clicked, this, &ChatInputBar::gameClicked);
    connect(history_btn_, &QPushButton::clicked, this, &ChatInputBar::loadHistoryClicked);
}

void ChatInputBar::setGameMode(bool enabled) {
    game_mode_ = enabled;
    text_edit_->setVisible(!enabled);
    send_btn_->setVisible(!enabled);
    file_btn_->setVisible(!enabled);
    history_btn_->setVisible(!enabled);
    rock_btn_->parentWidget()->setVisible(enabled);
}
