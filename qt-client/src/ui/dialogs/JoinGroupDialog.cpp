#include "JoinGroupDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>

JoinGroupDialog::JoinGroupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("加入群组");
    setMinimumSize(400, 300);

    auto *layout = new QVBoxLayout(this);

    // direct join by ID
    auto *directGroup = new QGroupBox("通过群号加入");
    auto *directLayout = new QHBoxLayout(directGroup);
    id_edit_ = new QLineEdit();
    id_edit_->setPlaceholderText("输入群号");
    auto *joinBtn = new QPushButton("加入");
    directLayout->addWidget(id_edit_);
    directLayout->addWidget(joinBtn);
    layout->addWidget(directGroup);

    connect(joinBtn, &QPushButton::clicked, this, &QDialog::accept);

    // invites
    auto *inviteGroup = new QGroupBox("群组邀请");
    auto *inviteLayout = new QVBoxLayout(inviteGroup);
    invite_list_ = new QListWidget();
    inviteLayout->addWidget(invite_list_);

    auto *inviteBtnLayout = new QHBoxLayout();
    auto *acceptBtn = new QPushButton("接受");
    auto *rejectBtn = new QPushButton("拒绝");
    inviteBtnLayout->addStretch();
    inviteBtnLayout->addWidget(acceptBtn);
    inviteBtnLayout->addWidget(rejectBtn);
    inviteLayout->addLayout(inviteBtnLayout);
    layout->addWidget(inviteGroup);

    connect(acceptBtn, &QPushButton::clicked, this, [this]() {
        int row = invite_list_->currentRow();
        if (row >= 0 && row < invites_.size()) {
            emit acceptInvite(invites_[row].group_id);
            invite_list_->takeItem(row);
            invites_.removeAt(row);
        }
    });

    connect(rejectBtn, &QPushButton::clicked, this, [this]() {
        int row = invite_list_->currentRow();
        if (row >= 0 && row < invites_.size()) {
            emit rejectInvite(invites_[row].group_id);
            invite_list_->takeItem(row);
            invites_.removeAt(row);
        }
    });
}

uint64_t JoinGroupDialog::groupId() const {
    return id_edit_->text().toULongLong();
}

void JoinGroupDialog::setInvites(const QVector<GroupInvite> &invites) {
    invites_ = invites;
    invite_list_->clear();
    for (const auto &i : invites) {
        invite_list_->addItem(i.group_name + " (来自用户 " + QString::number(i.inviter_id) + ")");
    }
}
