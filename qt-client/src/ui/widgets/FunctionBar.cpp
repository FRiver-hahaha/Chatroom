#include "FunctionBar.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QToolButton>
#include <QMenu>

FunctionBar::FunctionBar(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    auto addBtn = [&](const QString &text) -> QPushButton* {
        auto *btn = new QPushButton(text);
        btn->setMinimumHeight(32);
        layout->addWidget(btn);
        return btn;
    };

    // friend section
    auto *friendLabel = new QLabel("好友操作");
    friendLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(friendLabel);

    add_friend_btn_ = addBtn("添加好友");
    delete_friend_btn_ = addBtn("删除好友");

    // 拉黑好友：下拉菜单集成「拉黑 / 解除拉黑 / 黑名单列表」
    block_btn_ = new QToolButton();
    block_btn_->setText("拉黑好友");
    block_btn_->setPopupMode(QToolButton::InstantPopup);
    block_btn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    block_btn_->setMinimumHeight(32);
    auto *blockMenu = new QMenu(block_btn_);
    QAction *actBlock = blockMenu->addAction("拉黑好友（当前聊天）");
    QAction *actUnblock = blockMenu->addAction("解除拉黑");
    QAction *actQuery = blockMenu->addAction("黑名单列表");
    block_btn_->setMenu(blockMenu);
    layout->addWidget(block_btn_);
    friend_requests_btn_ = addBtn("好友请求");

    // group section
    auto *groupLabel = new QLabel("群组操作");
    groupLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(groupLabel);

    create_group_btn_ = addBtn("创建群组");
    join_group_btn_ = addBtn("加入群组");
    group_invites_btn_ = addBtn("群组邀请");

    quit_group_btn_ = addBtn("退出群组");
    view_members_btn_ = addBtn("查看成员");
    change_group_name_btn_ = addBtn("修改群名");

    approve_join_btn_ = addBtn("批准加入");
    remove_member_btn_ = addBtn("移除成员");

    add_admin_btn_ = addBtn("添加管理员");
    remove_admin_btn_ = addBtn("移除管理员");
    dismiss_group_btn_ = addBtn("解散群组");

    // account section
    auto *accountLabel = new QLabel("账号操作");
    accountLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(accountLabel);

    edit_profile_btn_ = addBtn("修改信息");
    delete_account_btn_ = addBtn("注销账号");
    delete_account_btn_->setStyleSheet(
        "QPushButton { color: red; min-height: 32px; }");

    layout->addStretch();

    scroll->setWidget(container);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scroll);

    // connections
    connect(add_friend_btn_, &QPushButton::clicked, this, &FunctionBar::addFriendClicked);
    connect(delete_friend_btn_, &QPushButton::clicked, this, &FunctionBar::deleteFriendClicked);
    connect(actBlock, &QAction::triggered, this, &FunctionBar::blockFriendClicked);
    connect(actUnblock, &QAction::triggered, this, &FunctionBar::unblockFriendClicked);
    connect(actQuery, &QAction::triggered, this, &FunctionBar::queryBlockedClicked);
    connect(friend_requests_btn_, &QPushButton::clicked, this, &FunctionBar::friendRequestsClicked);
    connect(create_group_btn_, &QPushButton::clicked, this, &FunctionBar::createGroupClicked);
    connect(join_group_btn_, &QPushButton::clicked, this, &FunctionBar::joinGroupClicked);
    connect(group_invites_btn_, &QPushButton::clicked, this, &FunctionBar::groupInvitesClicked);
    connect(quit_group_btn_, &QPushButton::clicked, this, &FunctionBar::quitGroupClicked);
    connect(view_members_btn_, &QPushButton::clicked, this, &FunctionBar::viewMembersClicked);
    connect(change_group_name_btn_, &QPushButton::clicked, this, &FunctionBar::changeGroupNameClicked);
    connect(approve_join_btn_, &QPushButton::clicked, this, &FunctionBar::approveJoinClicked);
    connect(remove_member_btn_, &QPushButton::clicked, this, &FunctionBar::removeMemberClicked);
    connect(add_admin_btn_, &QPushButton::clicked, this, &FunctionBar::addAdminClicked);
    connect(remove_admin_btn_, &QPushButton::clicked, this, &FunctionBar::removeAdminClicked);
    connect(dismiss_group_btn_, &QPushButton::clicked, this, &FunctionBar::dismissGroupClicked);
    connect(edit_profile_btn_, &QPushButton::clicked, this, &FunctionBar::editProfileClicked);
    connect(delete_account_btn_, &QPushButton::clicked, this, &FunctionBar::deleteAccountClicked);

    setGroupMode(false, "");
}

void FunctionBar::setGroupMode(bool isGroup, const QString &role) {
    bool showGeneral = !isGroup;
    create_group_btn_->setVisible(showGeneral);
    join_group_btn_->setVisible(showGeneral);
    group_invites_btn_->setVisible(showGeneral);

    bool showMember = isGroup;
    quit_group_btn_->setVisible(showMember);
    view_members_btn_->setVisible(showMember);
    change_group_name_btn_->setVisible(showMember);

    bool showAdmin = isGroup && (role == "admin" || role == "owner");
    approve_join_btn_->setVisible(showAdmin);
    remove_member_btn_->setVisible(showAdmin);

    bool showOwner = isGroup && role == "owner";
    add_admin_btn_->setVisible(showOwner);
    remove_admin_btn_->setVisible(showOwner);
    dismiss_group_btn_->setVisible(showOwner);
}
