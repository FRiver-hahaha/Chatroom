#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVector>

struct GroupInvite {
    uint64_t group_id;
    QString group_name;
    uint64_t inviter_id;
};

class JoinGroupDialog : public QDialog {
    Q_OBJECT
public:
    explicit JoinGroupDialog(QWidget *parent = nullptr);

    uint64_t groupId() const;
    void setInvites(const QVector<GroupInvite> &invites);

signals:
    void acceptInvite(uint64_t groupId);
    void rejectInvite(uint64_t groupId);

private:
    QLineEdit *id_edit_;
    QListWidget *invite_list_;
    QVector<GroupInvite> invites_;
};
