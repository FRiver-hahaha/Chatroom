#pragma once

#include <QObject>
#include <QByteArray>
#include <cstdint>

class FrameDecoder : public QObject {
    Q_OBJECT
public:
    explicit FrameDecoder(QObject *parent = nullptr);

    void feed(const QByteArray &data);

signals:
    void frameReady(const QByteArray &payload);
    void errorOccurred(const QString &message);

private:
    enum State { ReadingHeader, ReadingPayload };

    State state_ = ReadingHeader;
    uint32_t expected_len_ = 0;
    QByteArray buffer_;
};
