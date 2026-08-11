#include <QApplication>
#include <QCommandLineParser>
#include "network/ProtocolClient.h"
#include "state/ClientState.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/MomentWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ChatRoom");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("ChatRoom Qt Client");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {{"H", "host"}, "Server host", "host", "127.0.0.1"},
        {{"P", "port"}, "Server port", "port", "8080"},
    });
    parser.process(app);

    QString host = parser.value("host");
    quint16 port = parser.value("port").toUShort();
    if (port == 0) port = 8080;

    // network
    auto *client = new ProtocolClient();
    client->setParent(&app);

    // state
    auto *state = ClientState::instance();
    state->setProtocolClient(client);

    // login window
    auto *loginWindow = new LoginWindow();
    loginWindow->setServerAddress(host, port);
    loginWindow->show();

    // main window (hidden until login)
    auto *mainWindow = new MainWindow();

    // moments window
    auto *momentWindow = new MomentWindow();

    // login result
    QObject::connect(state, &ClientState::loginResult, loginWindow,
        [loginWindow, mainWindow, client, state](bool success, const QString &errorMsg) {
            if (success) {
                loginWindow->hide();
                mainWindow->onLoginSuccess();
            }
            Q_UNUSED(errorMsg)
        });

    // 确保已连接到登录框指定的服务器（未连接或地址变化时重连；QTcpSocket 会缓冲连接前的写入）
    auto ensureConnected = [client, loginWindow]() {
        quint16 port = loginWindow->port();
        QString host = loginWindow->host();
        if (host.isEmpty() || port == 0) return;
        if (client->isConnected() && client->host() == host && client->port() == port) return;
        client->disconnectFromServer();
        client->connectToServer(host, port);
    };

    // login
    QObject::connect(loginWindow, &LoginWindow::loginRequested, state,
        [state, ensureConnected](const QString &username, const QString &password) {
            ensureConnected();
            state->login(username, password);
        });

    // register
    QObject::connect(loginWindow, &LoginWindow::registerRequested, state,
        [state, ensureConnected](const QString &username, const QString &password, const QString &nickname) {
            ensureConnected();
            state->registerUser(username, password, nickname);
        });

    // register result forwarding
    QObject::connect(state, &ClientState::registerResult, loginWindow,
        &LoginWindow::onRegisterResult);

    // logout
    QObject::connect(state, &ClientState::logoutDone, mainWindow, [mainWindow, loginWindow]() {
        mainWindow->hide();
        loginWindow->show();
    });

    // delete account
    QObject::connect(state, &ClientState::deleteAccountResult, mainWindow,
        [mainWindow, loginWindow](bool success, const QString &) {
            if (success) {
                mainWindow->hide();
                loginWindow->show();
            }
        });

    // moments
    QObject::connect(mainWindow, &MainWindow::momentsClicked, momentWindow,
        [momentWindow]() { momentWindow->show(); momentWindow->raise(); });

    // 启动时预连接默认服务器；登录/注册时若地址变化会自动重连
    client->connectToServer(loginWindow->host(), loginWindow->port());

    return app.exec();
}
