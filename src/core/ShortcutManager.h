#pragma once

#include <QByteArray>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>

// One rebindable command. `id` is the stable key (stored in settings, used
// by MainWindow/SftpFileEditor to find their QAction/QShortcut); the label
// and category are what the Keyboard Shortcuts page shows.
struct ShortcutCommand
{
    // Two commands only conflict within the same scope: the SFTP file
    // editor is a window of its own, so it may reuse a key the main
    // window has.
    enum class Scope
    {
        MainWindow,
        FileEditor,
    };

    QString id;
    QString category;
    QString label;
    QKeySequence defaultSequence; // empty = unbound by default
    Scope scope = Scope::MainWindow;
};

// A user-defined shortcut: pressing it types `text` into the active
// terminal (e.g. a command you run often). Lives in the main window's scope.
struct CustomShortcut
{
    QString id; // "custom.<uuid>"
    QString name;
    QKeySequence sequence;
    QString text; // backslash escapes allowed - see decodeText()
    bool appendEnter = false;
};

// Every keyboard shortcut in the app, the built-in table plus the user's
// changes on top, persisted next to AppSettings' values (same ini file).
// Users of a shortcut ask sequence(id) and listen for shortcutsChanged()
// to re-apply it live - nothing hardcodes a key any more.
class ShortcutManager : public QObject
{
    Q_OBJECT

public:
    static ShortcutManager &instance();

    // ---- Built-in commands
    static const QList<ShortcutCommand> &commands();
    static const ShortcutCommand *command(const QString &id); // nullptr for an unknown (or custom) id

    // The effective key for a built-in or custom command; empty = unbound.
    QKeySequence sequence(const QString &id) const;
    bool isCustomized(const QString &id) const; // differs from the default (always false for a custom shortcut)
    void setSequence(const QString &id, const QKeySequence &sequence); // empty = unbind
    void resetToDefault(const QString &id);
    void resetAllToDefaults(); // built-ins only; custom shortcuts are kept

    // ---- Custom (send-text) shortcuts
    QList<CustomShortcut> customShortcuts() const { return m_custom; }
    void addCustom(const CustomShortcut &shortcut);
    void updateCustom(const CustomShortcut &shortcut);
    void removeCustom(const QString &id);
    static QString newCustomId();
    static bool isCustomId(const QString &id);

    // The id of whichever command (in the same scope as `id`) currently
    // uses `sequence`, other than `id` itself - or an empty string.
    QString conflictingCommand(const QString &id, const QKeySequence &sequence) const;
    QString labelFor(const QString &id) const;

    // Shortcuts are window-wide, so a bare letter (or Shift+letter) would
    // swallow ordinary typing everywhere. Ctrl/Alt/Meta or a function key
    // is required.
    static bool isAcceptable(const QKeySequence &sequence);

    // "ls -la\n", "\e[A", "\x03" -> the bytes to send. Understands \n \r \t
    // \e \\ and \xHH; anything else after a backslash is kept as written.
    static QByteArray decodeText(const QString &text);

signals:
    void shortcutsChanged();

private:
    ShortcutManager();

    void saveCustom();
    ShortcutCommand::Scope scopeOf(const QString &id) const;

    QSettings m_settings;
    QHash<QString, QKeySequence> m_overrides; // built-in id -> user's choice (possibly empty = unbound)
    QList<CustomShortcut> m_custom;
};
