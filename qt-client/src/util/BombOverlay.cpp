#include "BombOverlay.h"
#include <QPainter>
#include <QRandomGenerator>

BombOverlay::BombOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &BombOverlay::updateBombs);
}

void BombOverlay::start() {
    resize(parentWidget()->size());
    raise();

    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 5; ++i) {
        Bomb b;
        b.pos = QPointF(rng->bounded(width()), rng->bounded(height()));
        b.radius = 0;
        b.opacity = 1.0;
        b.maxRadius = rng->bounded(80) + 60;
        bombs_.append(b);
    }

    frame_count_ = 0;
    show();
    timer_->start(30);
    QTimer::singleShot(2500, this, &BombOverlay::stop);
}

void BombOverlay::stop() {
    timer_->stop();
    bombs_.clear();
    hide();
}

void BombOverlay::updateBombs() {
    frame_count_++;
    for (auto &b : bombs_) {
        b.radius += 2.5;
        b.opacity -= 0.015;
        if (b.opacity < 0) b.opacity = 0;
    }
    update();

    if (frame_count_ > 80) stop();
}

void BombOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // dark background flash
    painter.fillRect(rect(), QColor(255, 0, 0, 30));

    for (const auto &b : bombs_) {
        if (b.opacity <= 0) continue;
        QColor color(255, 60, 20, static_cast<int>(b.opacity * 180));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(b.pos, b.radius, b.radius);
    }
}
