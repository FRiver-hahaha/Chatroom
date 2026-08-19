#include "LoginWindow.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
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
    titleLabel->setStyleSheet("font-size: 28px; font-weight: bold; margin-bottom: 10px;");
    mainLayout->addWidget(titleLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setContentsMargins(50, 10, 50, 10);

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

    email_edit_ = new QLineEdit();
    email_edit_->setPlaceholderText("邮箱（仅注册/找回密码时需要）");
    formLayout->addRow("邮箱:", email_edit_);

    // 验证码：输入框 + 发送按钮
    auto *codeWidget = new QWidget();
    auto *codeLayout = new QHBoxLayout(codeWidget);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(4);
    code_edit_ = new QLineEdit();
    code_edit_->setPlaceholderText("验证码（仅注册时需要）");
    codeLayout->addWidget(code_edit_, 1);
    send_code_btn_ = new QPushButton("发送验证码");
    send_code_btn_->setFixedWidth(90);
    codeLayout->addWidget(send_code_btn_);
    formLayout->addRow("验证码:", codeWidget);

    mainLayout->addLayout(formLayout);

    // 忘记密码
    forgot_btn_ = new QPushButton("忘记密码？");
    forgot_btn_->setFlat(true);
    forgot_btn_->setStyleSheet("color: #2a6df4; font-size: 12px;");
    auto *forgotRow = new QHBoxLayout();
    forgotRow->addStretch();
    forgotRow->addWidget(forgot_btn_);
    forgotRow->addStretch();
    mainLayout->addLayout(forgotRow);

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
    status_label_->setWordWrap(true);
    mainLayout->addWidget(status_label_);

    connect(login_btn_, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(register_btn_, &QPushButton::clicked, this, &LoginWindow::onRegisterClicked);
    connect(send_code_btn_, &QPushButton::clicked, this, &LoginWindow::onSendVerifyCodeClicked);
    connect(forgot_btn_, &QPushButton::clicked, this, &LoginWindow::onForgotPasswordClicked);

    setWindowTitle("ChatRoom - 登录");
    setMinimumSize(420, 480);
}

void LoginWindow::setStatus(const QString &text, bool ok) {
    status_label_->setStyleSheet(ok ? "color: green;" : "color: red;");
    status_label_->setText(text);
}

void LoginWindow::onLoginClicked() {
    QString username = username_edit_->text().trimmed();
    QString password = password_edit_->text();
    if (host().isEmpty() || port() == 0) {
        setStatus("服务器地址或端口无效");
        return;
    }
    if (username.isEmpty() || password.isEmpty()) {
        setStatus("用户名和密码不能为空");
        return;
    }
    setStatus("正在登录...");
    emit loginRequested(username, password);
}

void LoginWindow::onRegisterClicked() {
    QString username = username_edit_->text().trimmed();
    QString password = password_edit_->text();
    QString nickname = nickname_edit_->text().trimmed();
    QString email = email_edit_->text().trimmed();
    QString code = code_edit_->text().trimmed();
    if (host().isEmpty() || port() == 0) {
        setStatus("服务器地址或端口无效");
        return;
    }
    if (username.isEmpty() || password.isEmpty() || nickname.isEmpty()) {
        setStatus("注册需要用户名、密码和昵称");
        return;
    }
    if (email.isEmpty()) {
        setStatus("注册需要填写邮箱");
        return;
    }
    if (code.isEmpty()) {
        setStatus("请先点击「发送验证码」获取邮箱验证码");
        return;
    }
    setStatus("正在注册...");
    emit registerRequested(username, password, nickname, email, code);
}

void LoginWindow::onSendVerifyCodeClicked() {
    QString email = email_edit_->text().trimmed();
    if (host().isEmpty() || port() == 0) {
        setStatus("服务器地址或端口无效");
        return;
    }
    if (email.isEmpty()) {
        setStatus("请先填写邮箱");
        return;
    }
    setStatus("验证码发送中...");
    emit verifyCodeRequested(email, "register");
}

void LoginWindow::onForgotPasswordClicked() {
    bool ok;
    QString email = QInputDialog::getText(this, "找回密码",
        "请输入注册邮箱（验证码将发送到该邮箱）:", QLineEdit::Normal, "", &ok);
    if (!ok || email.trimmed().isEmpty()) return;

    setStatus("验证码发送中...");
    emit verifyCodeRequested(email.trimmed(), "reset");
    pending_reset_email_ = email.trimmed();

    QString code = QInputDialog::getText(this, "找回密码",
        "请输入邮箱收到的验证码:", QLineEdit::Normal, "", &ok);
    if (!ok || code.trimmed().isEmpty()) return;

    QString newPass = QInputDialog::getText(this, "找回密码",
        "请输入新密码:", QLineEdit::Password, "", &ok);
    if (!ok || newPass.isEmpty()) return;

    emit passwordResetRequested(pending_reset_email_, code.trimmed(), newPass);
}

void LoginWindow::onLoginResult(bool success, const QString &errorMsg) {
    if (success) {
        setStatus("登录成功", true);
    } else {
        setStatus("登录失败: " + errorMsg);
    }
}

void LoginWindow::onRegisterResult(bool success, const QString &errorMsg) {
    if (success) {
        setStatus("注册成功，请登录", true);
    } else {
        setStatus("注册失败: " + errorMsg);
    }
}

void LoginWindow::onVerifyCodeResult(bool success, const QString &errorMsg) {
    if (success) {
        setStatus("验证码已发送，请查收邮箱", true);
    } else {
        setStatus("验证码发送失败: " + errorMsg);
    }
}

void LoginWindow::onPasswordResetResult(bool success, const QString &errorMsg) {
    if (success) {
        setStatus("密码已重置，请使用新密码登录", true);
    } else {
        setStatus("密码重置失败: " + errorMsg);
    }
}