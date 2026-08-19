#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class LoginWindow : public QWidget {
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);

    QString host() const;
    quint16 port() const;
    void setServerAddress(const QString &host, quint16 port);

signals:
    void loginRequested(const QString &username, const QString &password);
    void registerRequested(const QString &username, const QString &password,
                           const QString &nickname, const QString &email,
                           const QString &verifyCode);
    void verifyCodeRequested(const QString &target, const QString &scene);  // 渠道固定为 email
    void passwordResetRequested(const QString &email, const QString &verifyCode,
                                const QString &newPassword);

public slots:
    void onLoginResult(bool success, const QString &errorMsg);
    void onRegisterResult(bool success, const QString &errorMsg);
    void onVerifyCodeResult(bool success, const QString &errorMsg);
    void onPasswordResetResult(bool success, const QString &errorMsg);

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onSendVerifyCodeClicked();
    void onForgotPasswordClicked();

private:
    void setupUi();
    void setStatus(const QString &text, bool ok = false);

    QLineEdit *host_edit_;
    QLineEdit *port_edit_;
    QLineEdit *username_edit_;
    QLineEdit *password_edit_;
    QLineEdit *nickname_edit_;
    QLineEdit *email_edit_;
    QLineEdit *code_edit_;
    QPushButton *send_code_btn_;
    QPushButton *forgot_btn_;
    QPushButton *login_btn_;
    QPushButton *register_btn_;
    QLabel *status_label_;
    QString pending_reset_email_;
};