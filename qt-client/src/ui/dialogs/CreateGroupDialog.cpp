#include "CreateGroupDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>

CreateGroupDialog::CreateGroupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("创建群组");
    setMinimumWidth(350);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    name_edit_ = new QLineEdit();
    form->addRow("群组名称:", name_edit_);

    desc_edit_ = new QLineEdit();
    form->addRow("群组描述:", desc_edit_);

    public_check_ = new QCheckBox("公开群组");
    public_check_->setChecked(true);
    form->addRow("", public_check_);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString CreateGroupDialog::groupName() const { return name_edit_->text().trimmed(); }
QString CreateGroupDialog::description() const { return desc_edit_->text().trimmed(); }
bool CreateGroupDialog::isPublic() const { return public_check_->isChecked(); }
