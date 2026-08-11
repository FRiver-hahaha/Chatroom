#include "EditProfileDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>

EditProfileDialog::EditProfileDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("修改个人信息");
    setMinimumWidth(350);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    nickname_edit_ = new QLineEdit();
    nickname_edit_->setPlaceholderText("输入新昵称");
    form->addRow("昵称:", nickname_edit_);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString EditProfileDialog::newNickname() const {
    return nickname_edit_->text().trimmed();
}
