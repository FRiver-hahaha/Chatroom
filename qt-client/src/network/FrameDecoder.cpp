#include "FrameDecoder.h"
#include <arpa/inet.h>
#include <cstring>

FrameDecoder::FrameDecoder(QObject *parent)
    : QObject(parent) {}

void FrameDecoder::feed(const QByteArray &data) {
    buffer_.append(data);

    while (true) {
        if (state_ == ReadingHeader) {
            if (buffer_.size() < 4) return;

            uint32_t len;
            std::memcpy(&len, buffer_.constData(), 4);
            expected_len_ = ntohl(len);
            buffer_.remove(0, 4);

            if (expected_len_ > 256 * 1024 * 1024) {
                emit errorOccurred(QString("Frame too large: %1 bytes").arg(expected_len_));
                return;
            }
            state_ = ReadingPayload;
        }

        if (state_ == ReadingPayload) {
            if (buffer_.size() < static_cast<int>(expected_len_)) return;

            QByteArray frame = buffer_.left(expected_len_);
            buffer_.remove(0, expected_len_);
            state_ = ReadingHeader;

            emit frameReady(frame);
        }
    }
}
