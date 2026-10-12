#include "SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
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
    , m_activeColorButton(new QPushButton(this))
    , m_inactiveColorButton(new QPushButton(this))
    , m_paddingSpin(new QSpinBox(this))
    , m_gapSpin(new QSpinBox(this))
    , m_rightClickCombo(new QComboBox(this))
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

    // --- Split panes: colours, padding, gap
    connect(m_activeColorButton, &QPushButton::clicked, this, [this] { pickPaneColor(true); });
    connect(m_inactiveColorButton, &QPushButton::clicked, this, [this] { pickPaneColor(false); });

    m_paddingSpin->setRange(AppSettings::kMinPanePadding, AppSettings::kMaxPanePadding);
    m_paddingSpin->setSuffix(QStringLiteral(" px"));
    m_paddingSpin->setValue(AppSettings::instance().panePadding());
    m_paddingSpin->setToolTip(QStringLiteral("Space between a pane's border and its terminal. 0 puts the terminal right against the border."));
    connect(m_paddingSpin, &QSpinBox::valueChanged, this, &SettingsDialog::applyPaneAppearance);

    m_gapSpin->setRange(AppSettings::kMinPaneGap, AppSettings::kMaxPaneGap);
    m_gapSpin->setSuffix(QStringLiteral(" px"));
    m_gapSpin->setValue(AppSettings::instance().paneGap());
    m_gapSpin->setToolTip(QStringLiteral("Width of the draggable gap between split panes. At 0 the panes touch and there is nothing to drag - resize them back with a gap of 1 or more."));
    connect(m_gapSpin, &QSpinBox::valueChanged, this, &SettingsDialog::applyPaneAppearance);

    // --- Mouse
    m_rightClickCombo->addItem(QStringLiteral("Show the pane menu (split, close, copy, paste)"), static_cast<int>(AppSettings::RightClickAction::ShowMenu));
    m_rightClickCombo->addItem(QStringLiteral("Paste straight away"), static_cast<int>(AppSettings::RightClickAction::Paste));
    m_rightClickCombo->setCurrentIndex(qMax(0, m_rightClickCombo->findData(static_cast<int>(AppSettings::instance().rightClickAction()))));
    connect(m_rightClickCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::applyRightClickAction);

    refreshColorButtons();

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("App theme:"), m_themeCombo);
    form->addRow(QStringLiteral("Terminal color scheme:"), m_colorSchemeCombo);
    form->addRow(QStringLiteral("Terminal font:"), m_fontCombo);
    form->addRow(QStringLiteral("Font size:"), m_fontSizeSpin);
    form->addRow(QStringLiteral("Default keyboard profile:"), m_keyboardProfileCombo);

    auto *panesHeading = new QLabel(QStringLiteral("<b>Split panes</b>"), this);
    form->addRow(panesHeading);
    form->addRow(QStringLiteral("Active pane border:"), makeColorRow(m_activeColorButton, true));
    form->addRow(QStringLiteral("Other panes' border:"), makeColorRow(m_inactiveColorButton, false));
    form->addRow(QStringLiteral("Padding inside panes:"), m_paddingSpin);
    form->addRow(QStringLiteral("Gap between panes:"), m_gapSpin);

    auto *mouseHeading = new QLabel(QStringLiteral("<b>Mouse</b>"), this);
    form->addRow(mouseHeading);
    form->addRow(QStringLiteral("Right-click in a terminal:"), m_rightClickCombo);

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

void SettingsDialog::applyPaneAppearance()
{
    AppSettings::instance().setPanePadding(m_paddingSpin->value());
    AppSettings::instance().setPaneGap(m_gapSpin->value());
}

void SettingsDialog::applyRightClickAction()
{
    AppSettings::instance().setRightClickAction(static_cast<AppSettings::RightClickAction>(m_rightClickCombo->currentData().toInt()));
}

void SettingsDialog::pickPaneColor(bool active)
{
    AppSettings &settings = AppSettings::instance();
    const QPalette palette = QApplication::palette();
    QColor current = active ? settings.resolvedActivePaneColor(palette) : settings.resolvedInactivePaneColor(palette);
    if (current.alpha() == 0) // currently "None" - start the chooser from something visible
        current = palette.color(QPalette::Highlight);

    // Cancel/Esc leaves things as they were; "Auto" and "None" are the
    // buttons next to each swatch.
    QColorDialog chooser(current, this);
    chooser.setWindowTitle(active ? QStringLiteral("Active pane border") : QStringLiteral("Other panes' border"));
    if (chooser.exec() != QDialog::Accepted)
        return;

    QColor chosen = chooser.selectedColor();
    chosen.setAlpha(255); // a picked colour is always solid - "None" is its own button
    if (active)
        settings.setActivePaneColor(chosen);
    else
        settings.setInactivePaneColor(chosen);
    refreshColorButtons();
}

void SettingsDialog::setPaneColorChoice(bool active, const QColor &value)
{
    if (active)
        AppSettings::instance().setActivePaneColor(value);
    else
        AppSettings::instance().setInactivePaneColor(value);
    refreshColorButtons();
}

QWidget *SettingsDialog::makeColorRow(QPushButton *colorButton, bool active)
{
    // [ swatch + hex ............ ] [Auto] [None]
    auto *autoButton = new QPushButton(QStringLiteral("Auto"), this);
    autoButton->setToolTip(QStringLiteral("Follow the theme"));
    connect(autoButton, &QPushButton::clicked, this, [this, active] { setPaneColorChoice(active, QColor()); });

    auto *noneButton = new QPushButton(QStringLiteral("None"), this);
    noneButton->setToolTip(QStringLiteral("No border"));
    connect(noneButton, &QPushButton::clicked, this, [this, active] { setPaneColorChoice(active, AppSettings::noColor()); });

    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(colorButton, 1);
    layout->addWidget(autoButton);
    layout->addWidget(noneButton);
    return row;
}

void SettingsDialog::refreshColorButtons()
{
    // A swatch icon + text rather than a stylesheet background: the
    // stylesheet engine's colour defaults have burned this app's dark
    // theme before (see SftpDock's ".." button history).
    const AppSettings &settings = AppSettings::instance();
    const QPalette palette = QApplication::palette();

    auto setup = [&](QPushButton *button, const QColor &custom, const QColor &resolved) {
        QPixmap swatch(28, 14);
        swatch.fill(AppSettings::isNoColor(custom) ? palette.color(QPalette::Window) : resolved);
        QPainter p(&swatch);
        p.setPen(palette.color(QPalette::Mid));
        p.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
        p.end();

        button->setIcon(QIcon(swatch));
        button->setText(AppSettings::isNoColor(custom) ? QStringLiteral("None (no border)")
                                                       : (custom.isValid() ? custom.name().toUpper() : QStringLiteral("Automatic (theme)")));
        button->setToolTip(QStringLiteral("Click to choose a colour"));
    };
    setup(m_activeColorButton, settings.activePaneColor(), settings.resolvedActivePaneColor(palette));
    setup(m_inactiveColorButton, settings.inactivePaneColor(), settings.resolvedInactivePaneColor(palette));
}
