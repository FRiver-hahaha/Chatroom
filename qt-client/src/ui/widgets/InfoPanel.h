#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include "state/ClientState.h"

class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget *parent = nullptr);

    void showContactInfo(const ContactItem &contact);
    void showGroupMembers(const QStringList &members);
    void clear();

private:
    QLabel *name_label_;
    QLabel *detail_label_;
    QLabel *group_id_label_;
    QListWidget *member_list_;
    QWidget *friend_info_;
    QWidget *group_info_;
};
