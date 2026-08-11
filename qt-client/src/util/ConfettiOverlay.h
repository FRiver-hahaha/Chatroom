#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <QColor>

struct Confetti {
    QPointF pos;
    QPointF velocity;
    QColor color;
    qreal size;
    qreal rotation;
};

class ConfettiOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ConfettiOverlay(QWidget *parent = nullptr);

    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateConfetti();

private:
    QTimer *timer_;
    QVector<Confetti> particles_;
};
