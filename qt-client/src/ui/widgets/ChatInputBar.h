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

    // 最大发送文本长度（参考微信/QQ，5000 字）
    static constexpr int MaxTextLength = 5000;

signals:
    void sendClicked(const QString &text);
    void fileUploadClicked();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSendClicked();

private:
    void updateCharCount();

    QTextEdit *text_edit_;
    QPushButton *send_btn_;
    QPushButton *file_btn_;
    QLabel *char_count_label_;
};