#include "InfoPanel.h"
#include <QVBoxLayout>
#include <QDateTime>
#include <QGroupBox>

InfoPanel::InfoPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *titleLabel = new QLabel("详细信息");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(titleLabel);

    // friend info
    friend_info_ = new QWidget();
    auto *friendLayout = new QVBoxLayout(friend_info_);
    friendLayout->setContentsMargins(0, 0, 0, 0);

    name_label_ = new QLabel();
    name_label_->setStyleSheet("font-size: 15px; font-weight: bold;");
    friendLayout->addWidget(name_label_);

    detail_label_ = new QLabel();
    detail_label_->setWordWrap(true);
    detail_label_->setStyleSheet("font-size: 12px; color: #555;");
    friendLayout->addWidget(detail_label_);

    layout->addWidget(friend_info_);

    // group info
    group_info_ = new QWidget();
    auto *groupLayout = new QVBoxLayout(group_info_);
    groupLayout->setContentsMargins(0, 0, 0, 0);

    group_id_label_ = new QLabel();
    group_id_label_->setStyleSheet("font-size: 12px; color: #888;");
    groupLayout->addWidget(group_id_label_);

    auto *memberGroup = new QGroupBox("群成员");
    auto *memberGroupLayout = new QVBoxLayout(memberGroup);
    member_list_ = new QListWidget();
    memberGroupLayout->addWidget(member_list_);
    groupLayout->addWidget(memberGroup);

    layout->addWidget(group_info_);
    group_info_->hide();

    layout->addStretch();
}

void InfoPanel::showContactInfo(const ContactItem &contact) {
    if (contact.type == ContactItem::Friend) {
        friend_info_->show();
        group_info_->hide();

        name_label_->setText(contact.name);
        QString detail;
        detail += "用户ID: " + QString::number(contact.id) + "\n";
        detail += "状态: " + QString(contact.is_online ? "在线" : "离线") + "\n";
        detail += "火花: ";
        if (contact.streak_days > 0)
            detail += QString("已连续聊天 %1 天 🔥").arg(contact.streak_days);
        else
            detail += "未开始 (每天聊天即可点亮)";
        detail += "\n注册时间: " + QDateTime::fromSecsSinceEpoch(contact.add_time).toString("yyyy-MM-dd");
        detail_label_->setText(detail);
    } else {
        friend_info_->show();
        group_info_->show();

        name_label_->setText(contact.name);
        QString detail;
        detail += "群组ID: " + QString::number(contact.group_id) + "\n";
        detail += "群主ID: " + QString::number(contact.owner_id);
        detail_label_->setText(detail);

        group_id_label_->setText("群号: " + QString::number(contact.group_id));
    }
}

void InfoPanel::showGroupMembers(const QStringList &members) {
    member_list_->clear();
    for (const auto &m : members) {
        member_list_->addItem(m);
    }
    group_info_->show();
}

void InfoPanel::clear() {
    name_label_->clear();
    detail_label_->clear();
    group_id_label_->clear();
    member_list_->clear();
    friend_info_->show();
    group_info_->hide();
}
