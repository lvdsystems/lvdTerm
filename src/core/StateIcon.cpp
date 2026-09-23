#include "StateIcon.h"

#include <QPainter>
#include <QPixmap>

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

} // namespace StateIcon
