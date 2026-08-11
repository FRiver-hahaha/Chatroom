#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QMap>
#include <functional>
#include <memory>
#include "chatroom.pb.h"

class FrameDecoder;

class ProtocolClient : public QObject {
    Q_OBJECT
public:
    explicit ProtocolClient(QObject *parent = nullptr);
    ~ProtocolClient() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;
    QString host() const;
    quint16 port() const;

    void sendMessage(const chatroom::ChatMessage &msg);

signals:
    void connected();
    void disconnected();
    void messageReceived(const chatroom::ChatMessage &msg);
    void notificationReceived(const QString &text);
    void errorOccurred(const QString &message);

private slots:
    void onReadyRead();
    void onFrameReady(const QByteArray &payload);
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void writeFrame(const QByteArray &payload);

    QTcpSocket *socket_;
    FrameDecoder *decoder_;
    QString host_;
    quint16 port_ = 0;
};
