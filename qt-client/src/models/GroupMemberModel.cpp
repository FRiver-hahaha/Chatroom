#include "GroupMemberModel.h"

GroupMemberModel::GroupMemberModel(QObject *parent)
    : QAbstractListModel(parent) {}

int GroupMemberModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return static_cast<int>(members_.size());
}

QVariant GroupMemberModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= members_.size())
        return {};

    const auto &m = members_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return m.nickname.isEmpty() ? m.username : m.nickname;
    case UserIdRole:
        return QVariant::fromValue(m.user_id);
    case UsernameRole:
        return m.username;
    case NicknameRole:
        return m.nickname;
    case RoleRole:
        return m.role;
    case JoinTimeRole:
        return QVariant::fromValue(m.join_time);
    default:
        return {};
    }
}

void GroupMemberModel::setMembers(const QVector<GroupMemberItem> &members) {
    beginResetModel();
    members_ = members;
    endResetModel();
}
