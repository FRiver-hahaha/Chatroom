#include "ProtocolClient.h"
#include "FrameDecoder.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>

ProtocolClient::ProtocolClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , decoder_(new FrameDecoder(this))
{
    connect(socket_, &QTcpSocket::connected, this, &ProtocolClient::connected);
    connect(socket_, &QTcpSocket::disconnected, this, &ProtocolClient::disconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &ProtocolClient::onReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred, this, &ProtocolClient::onSocketError);
    connect(decoder_, &FrameDecoder::frameReady, this, &ProtocolClient::onFrameReady);
    connect(decoder_, &FrameDecoder::errorOccurred, this, &ProtocolClient::errorOccurred);
}

ProtocolClient::~ProtocolClient() {
    disconnectFromServer();
}

void ProtocolClient::connectToServer(const QString &host, quint16 port) {
    host_ = host;
    port_ = port;
    socket_->connectToHost(host, port);
}

void ProtocolClient::disconnectFromServer() {
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
    }
}

bool ProtocolClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState;
}

QString ProtocolClient::host() const {
    return host_;
}

quint16 ProtocolClient::port() const {
    return port_;
}

void ProtocolClient::sendMessage(const chatroom::ChatMessage &msg) {
    std::string serialized;
    if (!msg.SerializeToString(&serialized)) {
        emit errorOccurred("Failed to serialize message");
        return;
    }

    QByteArray payload(serialized.data(), static_cast<int>(serialized.size()));
    writeFrame(payload);
}

void ProtocolClient::writeFrame(const QByteArray &payload) {
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    QByteArray frame;
    frame.append(reinterpret_cast<const char *>(&len), 4);
    frame.append(payload);

    socket_->write(frame);
}

void ProtocolClient::onReadyRead() {
    QByteArray data = socket_->readAll();
    decoder_->feed(data);
}

void ProtocolClient::onFrameReady(const QByteArray &payload) {
    chatroom::ChatMessage msg;
    if (msg.ParseFromArray(payload.constData(), payload.size())) {
        emit messageReceived(msg);
    } else {
        QString text = QString::fromUtf8(payload);
        emit notificationReceived(text);
    }
}

void ProtocolClient::onSocketError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    emit errorOccurred(socket_->errorString());
}
