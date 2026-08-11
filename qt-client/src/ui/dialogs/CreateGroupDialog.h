#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

class CreateGroupDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateGroupDialog(QWidget *parent = nullptr);

    QString groupName() const;
    QString description() const;
    bool isPublic() const;

private:
    QLineEdit *name_edit_;
    QLineEdit *desc_edit_;
    QCheckBox *public_check_;
};
