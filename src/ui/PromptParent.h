#pragma once

#include <QApplication>
#include <QMainWindow>
#include <QWidget>

// The window a prompt raised from a connection (host key, wrong password, RDP/
// VNC credentials) should belong to. These prompts are application-modal and
// can arrive while lvdterm is in the background (a reconnect, a session being
// restored): with no parent, the dialog can open behind other windows, leaving
// a main window that ignores every click and looks hung. Owned by a visible
// window, it always stays above it, and clicking that window brings it forward.
inline QWidget *promptParent()
{
    if (QWidget *modal = QApplication::activeModalWidget())
        return modal; // stacked on the dialog that is already up
    if (QWidget *active = QApplication::activeWindow())
        return active;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible() && qobject_cast<QMainWindow *>(widget))
            return widget;
    }
    return nullptr;
}
