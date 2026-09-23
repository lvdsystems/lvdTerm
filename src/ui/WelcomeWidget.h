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
};
