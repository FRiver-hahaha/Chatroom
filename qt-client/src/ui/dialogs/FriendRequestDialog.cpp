#include "FriendRequestDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

FriendRequestDialog::FriendRequestDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("好友请求");
    setMinimumSize(400, 300);

    auto *layout = new QVBoxLayout(this);

    list_ = new QListWidget();
    layout->addWidget(list_);

    auto *btnLayout = new QHBoxLayout();
    approve_btn_ = new QPushButton("通过");
    reject_btn_ = new QPushButton("拒绝");
    btnLayout->addStretch();
    btnLayout->addWidget(approve_btn_);
    btnLayout->addWidget(reject_btn_);
    layout->addLayout(btnLayout);

    connect(approve_btn_, &QPushButton::clicked, this, [this]() {
        int row = list_->currentRow();
        if (row >= 0 && row < requests_.size()) {
            emit approved(requests_[row].user_id);
            list_->takeItem(row);
            requests_.removeAt(row);
        }
    });

    connect(reject_btn_, &QPushButton::clicked, this, [this]() {
        int row = list_->currentRow();
        if (row >= 0 && row < requests_.size()) {
            emit rejected(requests_[row].user_id);
            list_->takeItem(row);
            requests_.removeAt(row);
        }
    });
}

void FriendRequestDialog::setRequests(const QVector<FriendRequest> &requests) {
    requests_ = requests;
    list_->clear();
    for (const auto &r : requests) {
        QString name = r.nickname.isEmpty() ? r.username : r.nickname;
        list_->addItem(name + " (ID: " + QString::number(r.user_id) + ")");
    }
}
