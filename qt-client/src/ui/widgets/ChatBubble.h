#pragma once

#include <QWidget>
#include <QLabel>
#include <QString>
#include "state/ClientState.h"

class ChatBubble : public QWidget {
    Q_OBJECT
public:
    explicit ChatBubble(const MessageItem &msg, QWidget *parent = nullptr);

private:
    void setupUi(const MessageItem &msg);
};
