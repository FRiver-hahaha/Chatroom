#pragma once

#include <QWidget>
#include <QPushButton>
#include <QString>

class QToolButton;

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

    QPushButton *quit_group_btn_;
    QPushButton *view_members_btn_;
    QPushButton *change_group_name_btn_;
    QPushButton *approve_join_btn_;
    QPushButton *remove_member_btn_;
    QPushButton *add_admin_btn_;
    QPushButton *remove_admin_btn_;
    QPushButton *dismiss_group_btn_;

    QPushButton *edit_profile_btn_;
    QPushButton *delete_account_btn_;
};
