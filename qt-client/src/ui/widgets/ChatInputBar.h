#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>

class ChatInputBar : public QWidget {
    Q_OBJECT
public:
    explicit ChatInputBar(QWidget *parent = nullptr);

    void setGameMode(bool enabled);

signals:
    void sendClicked(const QString &text);
    void fileUploadClicked();
    void gameClicked();
    void loadHistoryClicked();

private:
    QTextEdit *text_edit_;
    QPushButton *send_btn_;
    QPushButton *file_btn_;
    QPushButton *game_btn_;
    QPushButton *history_btn_;
    QPushButton *rock_btn_;
    QPushButton *paper_btn_;
    QPushButton *scissors_btn_;
    bool game_mode_ = false;
};
