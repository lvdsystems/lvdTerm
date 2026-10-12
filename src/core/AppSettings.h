#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QObject>
#include <QSettings>

// Persisted appearance preferences (%APPDATA%/lvdterm/lvdterm/lvdterm.ini
// via QSettings), shared app-wide as a singleton so every open
// TerminalView can react live when SettingsDialog changes them, without
// MainWindow having to thread a settings object through everywhere.
class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings &instance();

    QString theme() const; // "Dark" or "Light" - the app's own Fusion palette, see Theme.h
    void setTheme(const QString &theme);

    QString colorScheme() const; // terminal color scheme name, see ColorSchemes.h
    void setColorScheme(const QString &name);

    QFont terminalFont() const;
    void setTerminalFont(const QFont &font);

    // See KeyboardProfiles.h - what an ad-hoc quick-connect (or a saved
    // profile with no per-connection override) uses. Defaults to
    // "default", one of KeyboardProfiles::names().
    QString defaultKeyboardProfile() const;
    void setDefaultKeyboardProfile(const QString &name);

    // ---- Split-pane look. The colours are "automatic" (invalid QColor)
    // by default: the theme's accent for the active pane, a lightened
    // window colour for the others - so they keep following the theme
    // until the user picks one explicitly.
    // "No border" is a fully transparent colour (alpha 0, see
    // isNoColor()): still a valid, saved choice, distinct from invalid
    // (= automatic).
    static QColor noColor() { return QColor(0, 0, 0, 0); }
    static bool isNoColor(const QColor &color) { return color.isValid() && color.alpha() == 0; }

    QColor activePaneColor() const;
    void setActivePaneColor(const QColor &color); // invalid = back to automatic
    QColor inactivePaneColor() const;
    void setInactivePaneColor(const QColor &color);
    // What the pane painters actually use: the custom colour, else the
    // automatic one derived from `palette`.
    QColor resolvedActivePaneColor(const QPalette &palette) const;
    QColor resolvedInactivePaneColor(const QPalette &palette) const;

    // Pixels between a pane's border (measured from its inner edge) and
    // its content; 0 puts the terminal right against the border.
    static constexpr int kMinPanePadding = 0;
    static constexpr int kDefaultPanePadding = 2;
    static constexpr int kMaxPanePadding = 24;
    int panePadding() const;
    void setPanePadding(int pixels);

    // The draggable gap between split panes.
    static constexpr int kMinPaneGap = 0; // 0 = panes touch, and there is no handle left to drag
    static constexpr int kMaxPaneGap = 24;
    int paneGap() const;
    void setPaneGap(int pixels);

    // What right-clicking inside a terminal does: open the pane menu
    // (split/close/copy/paste), or paste straight away (the original
    // behaviour).
    enum class RightClickAction
    {
        ShowMenu,
        Paste,
    };
    RightClickAction rightClickAction() const;
    void setRightClickAction(RightClickAction action);

    // Copy to the clipboard as soon as a text selection is made, instead
    // of needing Copy. Off by default. Read when a selection settles, so
    // no change signal.
    bool copyOnSelect() const;
    void setCopyOnSelect(bool enabled);

signals:
    void themeChanged(const QString &theme);
    void paneAppearanceChanged(); // pane colours, padding and/or gap
    void terminalAppearanceChanged(); // color scheme and/or font

private:
    AppSettings();

    QSettings m_settings;
};
