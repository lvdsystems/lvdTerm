#include "SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/Theme.h"
#include "core/AppSettings.h"
#include "terminal/ColorSchemes.h"
#include "terminal/KeyboardProfiles.h"

namespace
{
// KeyboardProfiles::names() returns lookup keys ("default", "linux",
// "vt100") - capitalize just for display, item data still carries the raw
// name AppSettings::setDefaultKeyboardProfile() expects.
QString prettyProfileName(const QString &name)
{
    QString pretty = name;
    if (!pretty.isEmpty())
        pretty[0] = pretty[0].toUpper();
    return pretty;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_themeCombo(new QComboBox(this))
    , m_colorSchemeCombo(new QComboBox(this))
    , m_fontCombo(new QFontComboBox(this))
    , m_fontSizeSpin(new QSpinBox(this))
    , m_keyboardProfileCombo(new QComboBox(this))
{
    setWindowTitle(QStringLiteral("Settings"));

    m_themeCombo->addItems({QStringLiteral("Dark"), QStringLiteral("Light")});
    m_themeCombo->setCurrentText(AppSettings::instance().theme());
    connect(m_themeCombo, &QComboBox::currentTextChanged, this, &SettingsDialog::applyTheme);

    m_colorSchemeCombo->addItems(ColorSchemes::names());
    m_colorSchemeCombo->setCurrentText(AppSettings::instance().colorScheme());
    connect(m_colorSchemeCombo, &QComboBox::currentTextChanged, this, &SettingsDialog::applyTerminalAppearance);

    // ScalableFonts excludes legacy bitmap/raster fonts (e.g. Windows'
    // own "Terminal" font) - see AppSettings::terminalFont()'s comment on
    // why one of those caused a real, reported GUI lag.
    m_fontCombo->setFontFilters(QFontComboBox::MonospacedFonts | QFontComboBox::ScalableFonts);
    m_fontCombo->setCurrentFont(AppSettings::instance().terminalFont());
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, &SettingsDialog::applyTerminalAppearance);

    m_fontSizeSpin->setRange(6, 36);
    m_fontSizeSpin->setValue(AppSettings::instance().terminalFont().pointSize());
    connect(m_fontSizeSpin, &QSpinBox::valueChanged, this, &SettingsDialog::applyTerminalAppearance);

    for (const QString &name : KeyboardProfiles::names())
        m_keyboardProfileCombo->addItem(prettyProfileName(name), name);
    m_keyboardProfileCombo->setCurrentIndex(qMax(0, m_keyboardProfileCombo->findData(AppSettings::instance().defaultKeyboardProfile())));
    connect(m_keyboardProfileCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::applyKeyboardProfile);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("App theme:"), m_themeCombo);
    form->addRow(QStringLiteral("Terminal color scheme:"), m_colorSchemeCombo);
    form->addRow(QStringLiteral("Terminal font:"), m_fontCombo);
    form->addRow(QStringLiteral("Font size:"), m_fontSizeSpin);
    form->addRow(QStringLiteral("Default keyboard profile:"), m_keyboardProfileCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void SettingsDialog::applyTheme()
{
    AppSettings::instance().setTheme(m_themeCombo->currentText());
    Theme::apply(m_themeCombo->currentText());
}

void SettingsDialog::applyTerminalAppearance()
{
    AppSettings::instance().setColorScheme(m_colorSchemeCombo->currentText());

    QFont font = m_fontCombo->currentFont();
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(m_fontSizeSpin->value());
    AppSettings::instance().setTerminalFont(font);
}

void SettingsDialog::applyKeyboardProfile()
{
    // Only affects new connections from here on - see AppSettings::
    // setDefaultKeyboardProfile()'s own comment.
    AppSettings::instance().setDefaultKeyboardProfile(m_keyboardProfileCombo->currentData().toString());
}
