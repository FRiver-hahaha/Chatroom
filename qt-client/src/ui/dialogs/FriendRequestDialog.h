#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVector>

struct FriendRequest {
    uint64_t user_id;
    QString username;
    QString nickname;
};

class FriendRequestDialog : public QDialog {
    Q_OBJECT
public:
    explicit FriendRequestDialog(QWidget *parent = nullptr);

    void setRequests(const QVector<FriendRequest> &requests);

signals:
    void approved(uint64_t userId); // add friend by ID
    void rejected(uint64_t userId);

private:
    QListWidget *list_;
    QPushButton *approve_btn_;
    QPushButton *reject_btn_;
    QVector<FriendRequest> requests_;
};
