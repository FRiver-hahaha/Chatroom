#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>

class QLabel;
class QKeyEvent;

class ChatInputBar : public QWidget {
    Q_OBJECT
public:
    explicit ChatInputBar(QWidget *parent = nullptr);

signals:
    void sendClicked(const QString &text);
    void fileUploadClicked();
    void loadHistoryClicked();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSendClicked();

private:
    QTextEdit *text_edit_;
    QPushButton *send_btn_;
    QPushButton *file_btn_;
    QPushButton *history_btn_;
    QLabel *char_count_label_;
};
