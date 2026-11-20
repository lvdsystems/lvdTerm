#include "StateIcon.h"

#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>

namespace StateIcon
{

QColor color(Transport::State state)
{
    switch (state) {
    case Transport::State::Connected:
        return QColor(0x2e, 0xa0, 0x43);
    case Transport::State::Connecting:
        return QColor(0xd9, 0xa4, 0x0f);
    case Transport::State::Error:
        return QColor(0xd9, 0x3f, 0x3f);
    case Transport::State::Disconnected:
        return QColor(0x90, 0x90, 0x90);
    }
    return QColor(0x90, 0x90, 0x90);
}

QIcon icon(Transport::State state)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color(state));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(1, 1, 10, 10);
    return QIcon(pixmap);
}

namespace
{
QPixmap tabPixmap(Transport::State state, bool activity, qreal dpr)
{
    constexpr int kSize = 16;
    QPixmap pixmap(qRound(kSize * dpr), qRound(kSize * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center(kSize / 2.0, kSize / 2.0);
    const QColor base = color(state);

    if (activity) {
        // Soft glow fading out from the dot's edge, plus a crisp ring so it
        // still reads at a glance on a light tab bar.
        QColor glow = base.lighter(150);
        QRadialGradient gradient(center, kSize / 2.0);
        glow.setAlpha(190);
        gradient.setColorAt(0.55, glow);
        glow.setAlpha(0);
        gradient.setColorAt(1.0, glow);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawEllipse(center, kSize / 2.0, kSize / 2.0);

        QColor ring = base.lighter(170);
        ring.setAlpha(230);
        painter.setPen(QPen(ring, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, 6.6, 6.6);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawEllipse(center, 5.0, 5.0);
    return pixmap;
}
} // namespace

QIcon tabIcon(Transport::State state, bool activity)
{
    QIcon icon;
    icon.addPixmap(tabPixmap(state, activity, 1.0));
    icon.addPixmap(tabPixmap(state, activity, 2.0));
    return icon;
}

} // namespace StateIcon
