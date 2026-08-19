#pragma once

#include <QWidget>
#include <QPushButton>
#include <QString>

class QToolButton;
class QAction;

class FunctionBar : public QWidget {
    Q_OBJECT
public:
    explicit FunctionBar(QWidget *parent = nullptr);

    void setGroupMode(bool isGroup, const QString &role);

signals:
    void addFriendClicked();
    void deleteFriendClicked();
    void blockFriendClicked();
    void unblockFriendClicked();
    void queryBlockedClicked();
    void friendRequestsClicked();
    void createGroupClicked();
    void joinGroupClicked();
    void groupInvitesClicked();
    void editProfileClicked();
    void deleteAccountClicked();

    // group-specific
    void quitGroupClicked();
    void viewMembersClicked();
    void changeGroupNameClicked();
    void approveJoinClicked();
    void removeMemberClicked();
    void addAdminClicked();
    void removeAdminClicked();
    void dismissGroupClicked();

private:
    QPushButton *add_friend_btn_;
    QPushButton *delete_friend_btn_;
    QToolButton *block_btn_;
    QPushButton *friend_requests_btn_;
    QPushButton *create_group_btn_;
    QPushButton *join_group_btn_;
    QPushButton *group_invites_btn_;

    // 群组管理：下拉菜单（仿拉黑好友）
    QToolButton *group_mgr_btn_;
    QAction *act_view_members_;
    QAction *act_change_name_;
    QAction *act_approve_join_;
    QAction *act_remove_member_;
    QAction *act_add_admin_;
    QAction *act_remove_admin_;
    QAction *act_dismiss_;
    QAction *act_quit_;

    QPushButton *edit_profile_btn_;
    QPushButton *delete_account_btn_;
};