#include "GameDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

GameDialog::GameDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("石头剪刀布 - 三局两胜");
    setMinimumSize(350, 300);

    auto *layout = new QVBoxLayout(this);

    status_label_ = new QLabel("选择你的出拳");
    status_label_->setAlignment(Qt::AlignCenter);
    status_label_->setStyleSheet("font-size: 16px; margin: 10px;");
    layout->addWidget(status_label_);

    score_label_ = new QLabel("你: 0  对手: 0");
    score_label_->setAlignment(Qt::AlignCenter);
    score_label_->setStyleSheet("font-size: 14px; color: #555;");
    layout->addWidget(score_label_);

    auto *btnLayout = new QHBoxLayout();
    rock_btn_ = new QPushButton("石头");
    paper_btn_ = new QPushButton("剪刀");
    scissors_btn_ = new QPushButton("布");
    rock_btn_->setMinimumHeight(60);
    paper_btn_->setMinimumHeight(60);
    scissors_btn_->setMinimumHeight(60);
    rock_btn_->setStyleSheet("font-size: 18px;");
    paper_btn_->setStyleSheet("font-size: 18px;");
    scissors_btn_->setStyleSheet("font-size: 18px;");
    btnLayout->addWidget(rock_btn_);
    btnLayout->addWidget(paper_btn_);
    btnLayout->addWidget(scissors_btn_);
    layout->addLayout(btnLayout);

    connect(rock_btn_, &QPushButton::clicked, this, &GameDialog::onRockClicked);
    connect(paper_btn_, &QPushButton::clicked, this, &GameDialog::onPaperClicked);
    connect(scissors_btn_, &QPushButton::clicked, this, &GameDialog::onScissorsClicked);
}

void GameDialog::onRockClicked() {
    emit gameMove("rock");
    status_label_->setText("你出了: 石头");
}

void GameDialog::onPaperClicked() {
    emit gameMove("scissors");
    status_label_->setText("你出了: 剪刀");
}

void GameDialog::onScissorsClicked() {
    emit gameMove("paper");
    status_label_->setText("你出了: 布");
}

void GameDialog::showResult(const QString &resultText, bool isWinner) {
    score_label_->setText(resultText);

    if (my_wins_ >= 2 || opponent_wins_ >= 2) {
        setEnabled(false);
        if (isWinner) {
            QMessageBox::information(this, "游戏结束", "恭喜你赢了! 撒花~");
        } else {
            QMessageBox::information(this, "游戏结束", "很遗憾你输了... 炸弹!");
        }
        accept();
    }
}
