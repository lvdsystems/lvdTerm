#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace
{
// The well-known "Fusion dark palette" recipe used by a great many Qt
// apps as their dark theme starting point.
QPalette darkPalette()
{
    QPalette palette;
    const QColor windowColor(53, 53, 53);
    const QColor baseColor(35, 35, 35);
    const QColor disabledColor(127, 127, 127);

    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, baseColor);
    palette.setColor(QPalette::AlternateBase, windowColor);
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledColor);
    palette.setColor(QPalette::Button, windowColor);
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledColor);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(42, 130, 218));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledColor);

    return palette;
}
} // namespace

void Theme::apply(const QString &name)
{
    if (name == QStringLiteral("Dark"))
        QApplication::setPalette(darkPalette());
    else
        QApplication::setPalette(QApplication::style()->standardPalette());
}
