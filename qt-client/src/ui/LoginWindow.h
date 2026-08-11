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
    void registerRequested(const QString &username, const QString &password, const QString &nickname);

public slots:
    void onLoginResult(bool success, const QString &errorMsg);
    void onRegisterResult(bool success, const QString &errorMsg);

private slots:
    void onLoginClicked();
    void onRegisterClicked();

private:
    void setupUi();

    QLineEdit *host_edit_;
    QLineEdit *port_edit_;
    QLineEdit *username_edit_;
    QLineEdit *password_edit_;
    QLineEdit *nickname_edit_;
    QPushButton *login_btn_;
    QPushButton *register_btn_;
    QLabel *status_label_;
};
