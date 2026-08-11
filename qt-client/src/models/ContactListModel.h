#pragma once

#include <QAbstractListModel>
#include <QVector>
#include "state/ClientState.h"

class ContactListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ContactTypeRole = Qt::UserRole + 1,
        ContactIdRole,
        ContactNameRole,
        IsOnlineRole,
        GroupIdRole,
        OwnerIdRole,
        RoleStrRole
    };

    explicit ContactListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setContacts(const QVector<ContactItem> &contacts);

private:
    QVector<ContactItem> contacts_;
};
