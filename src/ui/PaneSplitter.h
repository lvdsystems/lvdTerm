#pragma once

#include <QEnterEvent>
#include <QPainter>
#include <QPalette>
#include <QSplitter>
#include <QSplitterHandle>

#include "core/AppSettings.h"
#include "terminal/ColorSchemes.h"

// A QSplitter whose handles are a visible gap between panes (width from
// AppSettings::paneGap()) rather than a hairline: the gap is what you drag to
// resize, and a small rounded grip in it - which lights up in the accent
// colour under the mouse - says so. Header-only; no signals, so no moc.
class PaneSplitterHandle : public QSplitterHandle
{
public:
    PaneSplitterHandle(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent) {}

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QSplitterHandle::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        QSplitterHandle::leaveEvent(event);
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // A 1-3 px gap has no room for a grip - it's just the divider.
        const bool vertical = orientation() == Qt::Horizontal;
        if ((vertical ? width() : height()) < 6)
            return;

        const bool hot = underMouse();
        const AppSettings &settings = AppSettings::instance();
        const QColor color = hot ? settings.resolvedActivePaneColor(palette()) : settings.resolvedInactivePaneColor(palette()).lighter(115);
        p.setPen(Qt::NoPen);
        p.setBrush(color);

        // A pill centred in the gap, running along it: the handle of a
        // horizontal splitter (panes side by side) is a vertical strip.
        const qreal thickness = 3;
        const qreal length = qMin<qreal>(hot ? 56 : 36, (vertical ? height() : width()) - 12);
        if (length <= 0)
            return;
        const QRectF pill = vertical ? QRectF((width() - thickness) / 2, (height() - length) / 2, thickness, length)
                                     : QRectF((width() - length) / 2, (height() - thickness) / 2, length, thickness);
        p.drawRoundedRect(pill, thickness / 2, thickness / 2);
    }
};

class PaneSplitter : public QSplitter
{
public:
    explicit PaneSplitter(Qt::Orientation orientation, QWidget *parent = nullptr) : QSplitter(orientation, parent)
    {
        setHandleWidth(AppSettings::instance().paneGap());

        // The gap shows the terminal's background, not the app window's
        // lighter grey, so it doesn't read as a band between the panes
        // (TerminalView paints the same colour behind its content).
        // Follows colour-scheme changes.
        connect(&AppSettings::instance(), &AppSettings::terminalAppearanceChanged, this, [this] { update(); });
    }

protected:
    QSplitterHandle *createHandle() override { return new PaneSplitterHandle(orientation(), this); }

    // Painted directly rather than via the palette: setPalette() would
    // propagate to every pane inside and change their colours too.
    void paintEvent(QPaintEvent *) override
    {
        QPainter(this).fillRect(rect(), ColorSchemes::background(AppSettings::instance().colorScheme()));
    }
};
