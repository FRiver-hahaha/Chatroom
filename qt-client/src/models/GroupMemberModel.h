#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

struct GroupMemberItem {
    uint64_t user_id = 0;
    QString username;
    QString nickname;
    QString role;
    uint64_t join_time = 0;
};

class GroupMemberModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        UserIdRole = Qt::UserRole + 1,
        UsernameRole,
        NicknameRole,
        RoleRole,
        JoinTimeRole
    };

    explicit GroupMemberModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setMembers(const QVector<GroupMemberItem> &members);

private:
    QVector<GroupMemberItem> members_;
};
