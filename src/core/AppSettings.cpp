#include "AppSettings.h"

#include <QFontDatabase>

namespace
{
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kColorSchemeKey = "appearance/colorScheme";
constexpr auto kFontFamilyKey = "appearance/fontFamily";
constexpr auto kFontPointSizeKey = "appearance/fontPointSize";
constexpr auto kKeyboardProfileKey = "terminal/keyboardProfile";

constexpr auto kDefaultTheme = "Dark";
constexpr auto kDefaultColorScheme = "Dark";
constexpr auto kFallbackFontFamily = "Consolas";
constexpr int kDefaultFontPointSize = 10;
constexpr auto kDefaultKeyboardProfile = "default";

// Plain Consolas (always present on Windows) - reverted from a "prefer
// Cascadia Mono" heuristic per explicit feedback.
QString defaultFontFamily()
{
    return QString::fromLatin1(kFallbackFontFamily);
}
} // namespace

AppSettings &AppSettings::instance()
{
    static AppSettings instance;
    return instance;
}

AppSettings::AppSettings() : m_settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lvdterm"), QStringLiteral("lvdterm")) { }

QString AppSettings::theme() const
{
    return m_settings.value(QLatin1String(kThemeKey), QString::fromLatin1(kDefaultTheme)).toString();
}

void AppSettings::setTheme(const QString &theme)
{
    if (theme == this->theme())
        return;
    m_settings.setValue(QLatin1String(kThemeKey), theme);
    emit themeChanged(theme);
}

QString AppSettings::colorScheme() const
{
    return m_settings.value(QLatin1String(kColorSchemeKey), QString::fromLatin1(kDefaultColorScheme)).toString();
}

void AppSettings::setColorScheme(const QString &name)
{
    if (name == colorScheme())
        return;
    m_settings.setValue(QLatin1String(kColorSchemeKey), name);
    emit terminalAppearanceChanged();
}

QFont AppSettings::terminalFont() const
{
    QString family = m_settings.value(QLatin1String(kFontFamilyKey), defaultFontFamily()).toString();
    // A non-scalable (bitmap/raster) family - e.g. the legacy Windows
    // "Terminal" font, which QFontComboBox::MonospacedFonts still listed
    // as an option - has no real outline glyphs, so Qt's font engine has
    // to fall back to a much slower per-glyph path to paint or size it at
    // an arbitrary point size. TerminalDisplay issues that call once per
    // character cell on every screen update, so this alone was enough to
    // make the whole GUI visibly lag. Silently fall back to the real
    // default instead of honoring a previously-saved bad choice.
    if (!QFontDatabase::isScalable(family))
        family = defaultFontFamily();
    QFont font(family);
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(m_settings.value(QLatin1String(kFontPointSizeKey), kDefaultFontPointSize).toInt());
    return font;
}

void AppSettings::setTerminalFont(const QFont &font)
{
    m_settings.setValue(QLatin1String(kFontFamilyKey), font.family());
    m_settings.setValue(QLatin1String(kFontPointSizeKey), font.pointSize());
    emit terminalAppearanceChanged();
}

QString AppSettings::defaultKeyboardProfile() const
{
    return m_settings.value(QLatin1String(kKeyboardProfileKey), QString::fromLatin1(kDefaultKeyboardProfile)).toString();
}

void AppSettings::setDefaultKeyboardProfile(const QString &name)
{
    // Only affects new connections from here on (the keyboard profile is
    // selected once per session, when it starts - see TerminalSession::
    // setKeyboardProfile()), so no live-apply signal is needed, unlike
    // theme/colorScheme/terminalFont.
    m_settings.setValue(QLatin1String(kKeyboardProfileKey), name);
}
