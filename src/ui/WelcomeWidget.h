#pragma once

#include <QWidget>

// Startup tab shown when there's no session to restore (see
// MainWindow::MainWindow() and SessionStore) instead of auto-starting a
// loopback connection. Not a TerminalView - Split/Close Pane/Find/Log
// already no-op safely on a non-TerminalView tab (currentActiveView()
// returning nullptr is handled everywhere it's checked).
class WelcomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget *parent = nullptr);

    // Its natural minimum (the unwrapped subtitle + logo card) is ~460 px,
    // and a QTabWidget's minimum is that of its widest page - so even
    // when another tab is showing, this one kept the whole window from
    // being narrower than that (e.g. snapped to half a screen). Clipping
    // when squeezed is fine for a splash page.
    QSize minimumSizeHint() const override { return QSize(120, 100); }
};
