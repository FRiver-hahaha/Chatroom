#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

class EditProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditProfileDialog(QWidget *parent = nullptr);

    QString newNickname() const;

private:
    QLineEdit *nickname_edit_;
};
