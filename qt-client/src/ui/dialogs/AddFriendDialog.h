#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

class AddFriendDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddFriendDialog(QWidget *parent = nullptr);
    uint64_t userId() const;
    QString email() const;

private:
    QLineEdit *id_edit_;
    QLineEdit *email_edit_;
    QPushButton *ok_btn_;
};
