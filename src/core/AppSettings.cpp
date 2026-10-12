#include "AppSettings.h"

#include <QFontDatabase>

namespace
{
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kColorSchemeKey = "appearance/colorScheme";
constexpr auto kFontFamilyKey = "appearance/fontFamily";
constexpr auto kFontPointSizeKey = "appearance/fontPointSize";
constexpr auto kKeyboardProfileKey = "terminal/keyboardProfile";
constexpr auto kActivePaneColorKey = "panes/activeColor";
constexpr auto kInactivePaneColorKey = "panes/inactiveColor";
constexpr auto kPanePaddingKey = "panes/padding";
constexpr auto kPaneGapKey = "panes/gap";
constexpr auto kRightClickKey = "mouse/rightClick";

constexpr auto kDefaultTheme = "Dark";
constexpr auto kDefaultColorScheme = "Dark";
constexpr int kDefaultFontPointSize = 10;
constexpr auto kDefaultKeyboardProfile = "default";

// Plain Consolas on Windows (always present there) - reverted from a
// "prefer Cascadia Mono" heuristic per explicit feedback, so this stays
// a flat literal, not reintroduced cleverness. Consolas doesn't exist on
// Linux at all, though, so there's nothing to hardcode equivalently
// there - QFontDatabase::systemFont(FixedFont) is Qt's own "give me a
// monospace font" query, the natural non-Windows equivalent of "just
// pick the one that's always there."
QString defaultFontFamily()
{
#ifdef _WIN32
    return QStringLiteral("Consolas");
#else
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
#endif
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

QColor AppSettings::activePaneColor() const
{
    return QColor(m_settings.value(QLatin1String(kActivePaneColorKey)).toString()); // invalid if unset/empty
}

void AppSettings::setActivePaneColor(const QColor &color)
{
    m_settings.setValue(QLatin1String(kActivePaneColorKey), color.isValid() ? color.name(QColor::HexArgb) : QString());
    emit paneAppearanceChanged();
}

QColor AppSettings::inactivePaneColor() const
{
    return QColor(m_settings.value(QLatin1String(kInactivePaneColorKey)).toString());
}

void AppSettings::setInactivePaneColor(const QColor &color)
{
    m_settings.setValue(QLatin1String(kInactivePaneColorKey), color.isValid() ? color.name(QColor::HexArgb) : QString());
    emit paneAppearanceChanged();
}

QColor AppSettings::resolvedActivePaneColor(const QPalette &palette) const
{
    const QColor custom = activePaneColor();
    return custom.isValid() ? custom : palette.color(QPalette::Highlight);
}

QColor AppSettings::resolvedInactivePaneColor(const QPalette &palette) const
{
    // Window lightened rather than QPalette::Mid, which is near-white in
    // the dark theme.
    const QColor custom = inactivePaneColor();
    return custom.isValid() ? custom : palette.color(QPalette::Window).lighter(170);
}

int AppSettings::panePadding() const
{
    return qBound(kMinPanePadding, m_settings.value(QLatin1String(kPanePaddingKey), kDefaultPanePadding).toInt(), kMaxPanePadding);
}

void AppSettings::setPanePadding(int pixels)
{
    m_settings.setValue(QLatin1String(kPanePaddingKey), qBound(kMinPanePadding, pixels, kMaxPanePadding));
    emit paneAppearanceChanged();
}

int AppSettings::paneGap() const
{
    return qBound(kMinPaneGap, m_settings.value(QLatin1String(kPaneGapKey), 8).toInt(), kMaxPaneGap);
}

void AppSettings::setPaneGap(int pixels)
{
    m_settings.setValue(QLatin1String(kPaneGapKey), qBound(kMinPaneGap, pixels, kMaxPaneGap));
    emit paneAppearanceChanged();
}

AppSettings::RightClickAction AppSettings::rightClickAction() const
{
    return m_settings.value(QLatin1String(kRightClickKey), QStringLiteral("menu")).toString() == QStringLiteral("paste") ? RightClickAction::Paste
                                                                                                                         : RightClickAction::ShowMenu;
}

void AppSettings::setRightClickAction(RightClickAction action)
{
    // Read fresh on every right-click, so no signal is needed.
    m_settings.setValue(QLatin1String(kRightClickKey), action == RightClickAction::Paste ? QStringLiteral("paste") : QStringLiteral("menu"));
}
