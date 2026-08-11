#include "LoginWindow.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QRegularExpressionValidator>

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

QString LoginWindow::host() const {
    return host_edit_->text().trimmed();
}

quint16 LoginWindow::port() const {
    return port_edit_->text().trimmed().toUShort();
}

void LoginWindow::setServerAddress(const QString &host, quint16 port) {
    host_edit_->setText(host);
    port_edit_->setText(QString::number(port));
}

void LoginWindow::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);

    auto *titleLabel = new QLabel("ChatRoom");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 28px; font-weight: bold; margin-bottom: 20px;");
    mainLayout->addWidget(titleLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setContentsMargins(50, 20, 50, 20);

    host_edit_ = new QLineEdit("127.0.0.1");
    host_edit_->setPlaceholderText("服务器地址");
    host_edit_->setMinimumWidth(250);
    formLayout->addRow("服务器:", host_edit_);

    port_edit_ = new QLineEdit("8080");
    port_edit_->setPlaceholderText("端口");
    port_edit_->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,5}"), port_edit_));
    formLayout->addRow("端口:", port_edit_);

    username_edit_ = new QLineEdit();
    username_edit_->setPlaceholderText("用户名");
    username_edit_->setMinimumWidth(250);
    formLayout->addRow("用户名:", username_edit_);

    password_edit_ = new QLineEdit();
    password_edit_->setPlaceholderText("密码");
    password_edit_->setEchoMode(QLineEdit::Password);
    formLayout->addRow("密码:", password_edit_);

    nickname_edit_ = new QLineEdit();
    nickname_edit_->setPlaceholderText("昵称（仅注册时需要）");
    formLayout->addRow("昵称:", nickname_edit_);

    mainLayout->addLayout(formLayout);

    auto *btnLayout = new QHBoxLayout();
    login_btn_ = new QPushButton("登录");
    login_btn_->setMinimumWidth(100);
    register_btn_ = new QPushButton("注册");
    register_btn_->setMinimumWidth(100);
    btnLayout->addStretch();
    btnLayout->addWidget(login_btn_);
    btnLayout->addWidget(register_btn_);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    status_label_ = new QLabel();
    status_label_->setAlignment(Qt::AlignCenter);
    status_label_->setStyleSheet("color: red;");
    mainLayout->addWidget(status_label_);

    connect(login_btn_, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(register_btn_, &QPushButton::clicked, this, &LoginWindow::onRegisterClicked);

    setWindowTitle("ChatRoom - 登录");
    setMinimumSize(400, 350);
}

void LoginWindow::onLoginClicked() {
    QString username = username_edit_->text().trimmed();
    QString password = password_edit_->text();
    if (host().isEmpty() || port() == 0) {
        status_label_->setText("服务器地址或端口无效");
        return;
    }
    if (username.isEmpty() || password.isEmpty()) {
        status_label_->setText("用户名和密码不能为空");
        return;
    }
    status_label_->setText("正在登录...");
    emit loginRequested(username, password);
}

void LoginWindow::onRegisterClicked() {
    QString username = username_edit_->text().trimmed();
    QString password = password_edit_->text();
    QString nickname = nickname_edit_->text().trimmed();
    if (host().isEmpty() || port() == 0) {
        status_label_->setText("服务器地址或端口无效");
        return;
    }
    if (username.isEmpty() || password.isEmpty() || nickname.isEmpty()) {
        status_label_->setText("注册需要用户名、密码和昵称");
        return;
    }
    status_label_->setText("正在注册...");
    emit registerRequested(username, password, nickname);
}

void LoginWindow::onLoginResult(bool success, const QString &errorMsg) {
    if (success) {
        status_label_->setStyleSheet("color: green;");
        status_label_->setText("登录成功");
    } else {
        status_label_->setStyleSheet("color: red;");
        status_label_->setText("登录失败: " + errorMsg);
    }
}

void LoginWindow::onRegisterResult(bool success, const QString &errorMsg) {
    if (success) {
        status_label_->setStyleSheet("color: green;");
        status_label_->setText("注册成功，请登录");
    } else {
        status_label_->setStyleSheet("color: red;");
        status_label_->setText("注册失败: " + errorMsg);
    }
}
