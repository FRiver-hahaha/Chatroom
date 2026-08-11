#pragma once

#include <QDialog>
#include <QPushButton>
#include <QLabel>

class GameDialog : public QDialog {
    Q_OBJECT
public:
    explicit GameDialog(QWidget *parent = nullptr);

signals:
    void gameMove(const QString &move); // "rock", "paper", "scissors"

private slots:
    void onRockClicked();
    void onPaperClicked();
    void onScissorsClicked();

public slots:
    void showResult(const QString &resultText, bool isWinner);

private:
    QLabel *status_label_;
    QLabel *score_label_;
    QPushButton *rock_btn_;
    QPushButton *paper_btn_;
    QPushButton *scissors_btn_;
    int my_wins_ = 0;
    int opponent_wins_ = 0;
};
