#pragma once

#include <QDialog>

class QColor;
class QComboBox;
class QFontComboBox;
class QPushButton;
class QSpinBox;

// Appearance and mouse settings - applies live via AppSettings as the user changes
// each control, so open terminal panes update immediately rather than
// needing an OK/Apply step.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void applyTheme();
    void applyTerminalAppearance();
    void applyKeyboardProfile();
    void applyPaneAppearance();
    void applyRightClickAction();
    void pickPaneColor(bool active); // opens a colour chooser, then applies it live
    void setPaneColorChoice(bool active, const QColor &value); // invalid = Auto, AppSettings::noColor() = None
    QWidget *makeColorRow(QPushButton *colorButton, bool active);
    void refreshColorButtons();

    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_colorSchemeCombo = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QComboBox *m_keyboardProfileCombo = nullptr;

    QPushButton *m_activeColorButton = nullptr;
    QPushButton *m_inactiveColorButton = nullptr;
    QSpinBox *m_paddingSpin = nullptr;
    QSpinBox *m_gapSpin = nullptr;
    QComboBox *m_rightClickCombo = nullptr;
};
