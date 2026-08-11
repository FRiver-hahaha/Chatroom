#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QPointF>

struct Bomb {
    QPointF pos;
    qreal radius;
    qreal opacity;
    qreal maxRadius;
};

class BombOverlay : public QWidget {
    Q_OBJECT
public:
    explicit BombOverlay(QWidget *parent = nullptr);

    void start();
    void stop();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateBombs();

private:
    QTimer *timer_;
    QVector<Bomb> bombs_;
    int frame_count_ = 0;
};
