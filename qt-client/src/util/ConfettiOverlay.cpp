#include "ConfettiOverlay.h"
#include <QPainter>
#include <QRandomGenerator>
#include <cmath>

ConfettiOverlay::ConfettiOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &ConfettiOverlay::updateConfetti);
}

void ConfettiOverlay::start() {
    resize(parentWidget()->size());
    raise();

    static const QColor colors[] = {
        QColor("#ff6b6b"), QColor("#ffd93d"), QColor("#6bcb77"),
        QColor("#4d96ff"), QColor("#ff922b"), QColor("#cc5de8"),
        QColor("#20c997"), QColor("#f06595")
    };

    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 80; ++i) {
        Confetti c;
        c.pos = QPointF(rng->bounded(width()), -rng->bounded(200));
        c.velocity = QPointF(rng->bounded(60) - 30, rng->bounded(100) + 50);
        c.color = colors[rng->bounded(8)];
        c.size = rng->bounded(5) + 4;
        c.rotation = rng->bounded(360);
        particles_.append(c);
    }

    show();
    timer_->start(30);
    QTimer::singleShot(3000, this, &ConfettiOverlay::stop);
}

void ConfettiOverlay::stop() {
    timer_->stop();
    particles_.clear();
    hide();
}

void ConfettiOverlay::updateConfetti() {
    for (auto &c : particles_) {
        c.pos += c.velocity * 0.03;
        c.velocity.ry() += 1.5; // gravity
        c.rotation += 2.0;
    }
    update();
}

void ConfettiOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto &c : particles_) {
        painter.save();
        painter.translate(c.pos);
        painter.rotate(c.rotation);
        painter.fillRect(QRectF(-c.size / 2, -c.size / 2, c.size, c.size * 0.6), c.color);
        painter.restore();
    }
}
