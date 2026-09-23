#pragma once

#include <QDialog>

class QComboBox;
class QFontComboBox;
class QSpinBox;

// Appearance settings - applies live via AppSettings as the user changes
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

    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_colorSchemeCombo = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QComboBox *m_keyboardProfileCombo = nullptr;
};
