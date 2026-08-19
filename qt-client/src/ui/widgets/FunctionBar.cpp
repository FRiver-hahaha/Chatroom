#include "FunctionBar.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QAction>

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
    block_btn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    block_btn_->setMinimumWidth(0);
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

    // 群组管理：下拉菜单集成成员/审批/管理员/解散/退出（仿拉黑好友）
    group_mgr_btn_ = new QToolButton();
    group_mgr_btn_->setText("群组管理");
    group_mgr_btn_->setPopupMode(QToolButton::InstantPopup);
    group_mgr_btn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    group_mgr_btn_->setMinimumHeight(32);
    group_mgr_btn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    group_mgr_btn_->setMinimumWidth(0);
    group_mgr_btn_->setMinimumHeight(32);
    auto *groupMenu = new QMenu(group_mgr_btn_);
    act_view_members_ = groupMenu->addAction("查看成员");
    act_change_name_ = groupMenu->addAction("修改群名");
    act_approve_join_ = groupMenu->addAction("审批入群申请");
    act_remove_member_ = groupMenu->addAction("移除成员");
    act_add_admin_ = groupMenu->addAction("设置管理员");
    act_remove_admin_ = groupMenu->addAction("取消管理员");
    act_dismiss_ = groupMenu->addAction("解散群组");
    act_quit_ = groupMenu->addAction("退出群组");
    group_mgr_btn_->setMenu(groupMenu);
    layout->addWidget(group_mgr_btn_);

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
    connect(act_view_members_, &QAction::triggered, this, &FunctionBar::viewMembersClicked);
    connect(act_change_name_, &QAction::triggered, this, &FunctionBar::changeGroupNameClicked);
    connect(act_approve_join_, &QAction::triggered, this, &FunctionBar::approveJoinClicked);
    connect(act_remove_member_, &QAction::triggered, this, &FunctionBar::removeMemberClicked);
    connect(act_add_admin_, &QAction::triggered, this, &FunctionBar::addAdminClicked);
    connect(act_remove_admin_, &QAction::triggered, this, &FunctionBar::removeAdminClicked);
    connect(act_dismiss_, &QAction::triggered, this, &FunctionBar::dismissGroupClicked);
    connect(act_quit_, &QAction::triggered, this, &FunctionBar::quitGroupClicked);
    connect(edit_profile_btn_, &QPushButton::clicked, this, &FunctionBar::editProfileClicked);
    connect(delete_account_btn_, &QPushButton::clicked, this, &FunctionBar::deleteAccountClicked);

    setGroupMode(false, "");
}

void FunctionBar::setGroupMode(bool isGroup, const QString &role) {
    bool showGeneral = !isGroup;
    create_group_btn_->setVisible(showGeneral);
    join_group_btn_->setVisible(showGeneral);
    group_invites_btn_->setVisible(showGeneral);
    group_mgr_btn_->setVisible(isGroup);

    bool showMember = isGroup;
    act_view_members_->setVisible(showMember);
    act_change_name_->setVisible(showMember);
    act_quit_->setVisible(showMember);

    bool showAdmin = isGroup && (role == "admin" || role == "owner");
    act_approve_join_->setVisible(showAdmin);
    act_remove_member_->setVisible(showAdmin);

    bool showOwner = isGroup && role == "owner";
    act_add_admin_->setVisible(showOwner);
    act_remove_admin_->setVisible(showOwner);
    act_dismiss_->setVisible(showOwner);
}