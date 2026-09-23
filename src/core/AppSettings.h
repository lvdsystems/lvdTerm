#pragma once

#include <QFont>
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

signals:
    void themeChanged(const QString &theme);
    void terminalAppearanceChanged(); // color scheme and/or font

private:
    AppSettings();

    QSettings m_settings;
};
