#include "ContactListModel.h"

ContactListModel::ContactListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int ContactListModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return static_cast<int>(contacts_.size());
}

QVariant ContactListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= contacts_.size())
        return {};

    const auto &c = contacts_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case ContactNameRole:
        return c.name;
    case ContactTypeRole:
        return static_cast<int>(c.type);
    case ContactIdRole:
        return QVariant::fromValue(c.id);
    case IsOnlineRole:
        return c.is_online;
    case GroupIdRole:
        return QVariant::fromValue(c.group_id);
    case OwnerIdRole:
        return QVariant::fromValue(c.owner_id);
    case RoleStrRole:
        return c.role;
    default:
        return {};
    }
}

void ContactListModel::setContacts(const QVector<ContactItem> &contacts) {
    beginResetModel();
    contacts_ = contacts;
    endResetModel();
}
