#include "AddFriendDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>

AddFriendDialog::AddFriendDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("添加好友");
    setMinimumWidth(350);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    id_edit_ = new QLineEdit();
    id_edit_->setPlaceholderText("输入用户ID");
    form->addRow("用户ID:", id_edit_);

    email_edit_ = new QLineEdit();
    email_edit_->setPlaceholderText("或输入邮箱搜索（需服务端支持）");
    form->addRow("邮箱:", email_edit_);
    layout->addLayout(form);

    ok_btn_ = new QPushButton("添加");
    layout->addWidget(ok_btn_);

    connect(ok_btn_, &QPushButton::clicked, this, &QDialog::accept);
}

uint64_t AddFriendDialog::userId() const {
    return id_edit_->text().toULongLong();
}

QString AddFriendDialog::email() const {
    return email_edit_->text().trimmed();
}
