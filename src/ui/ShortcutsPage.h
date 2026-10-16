#pragma once

#include <QKeySequence>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// The "Keyboard Shortcuts" page of the Settings dialog: every command with
// its current key, change/clear/reset per command, and creating, editing
// and deleting custom shortcuts (a key that types a piece of text into the
// active terminal). Edits apply live through ShortcutManager.
class ShortcutsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutsPage(QWidget *parent = nullptr);

private:
    void rebuild(); // from ShortcutManager, keeping the selection and filter
    void applyFilter();
    void updateButtons();
    QString selectedId() const;

    void changeSelected();
    void clearSelected();
    void resetSelected();
    void resetAll();
    void addCustom();
    void editSelectedCustom();
    void deleteSelectedCustom();
    void activated(QTreeWidgetItem *item);

    // Applies `sequence` to `id` after checking it is acceptable and asking
    // what to do about a clash. False = the user backed out; nothing changed.
    bool assign(const QString &id, const QKeySequence &sequence);

    QLineEdit *m_filterEdit = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_changeButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_editCustomButton = nullptr;
    QPushButton *m_deleteCustomButton = nullptr;
};
