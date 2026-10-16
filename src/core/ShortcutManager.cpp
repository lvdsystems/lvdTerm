#include "ShortcutManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace
{
constexpr auto kGroup = "shortcuts";
constexpr auto kCustomKey = "shortcutsCustom";
constexpr auto kCustomPrefix = "custom.";

QKeySequence seq(const char *text)
{
    return QKeySequence(QString::fromLatin1(text), QKeySequence::PortableText);
}

QString toStored(const QKeySequence &sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

// QSettings' ini backend turns "a,b" into a QStringList on read, which is
// exactly what a sequence like "Ctrl+," looks like - put it back together.
QString asString(const QVariant &value)
{
    return value.metaType().id() == QMetaType::QStringList ? value.toStringList().join(QLatin1Char(',')) : value.toString();
}

// Only the first chord: "Ctrl+K, Ctrl+C"-style sequences stay out of this
// (they make "is this key taken" and the UI needlessly hard).
QKeySequence firstChord(const QKeySequence &sequence)
{
    return sequence.isEmpty() ? QKeySequence() : QKeySequence(sequence[0]);
}

const QList<ShortcutCommand> &table()
{
    using Scope = ShortcutCommand::Scope;
    static const QList<ShortcutCommand> commands = {
        {QStringLiteral("connection.newLocal"), QStringLiteral("Connection"), QStringLiteral("New Local Terminal"), seq("Ctrl+T"), Scope::MainWindow},
        {QStringLiteral("connection.newSerial"), QStringLiteral("Connection"), QStringLiteral("New Serial Connection..."), {}, Scope::MainWindow},
        {QStringLiteral("connection.newTelnet"), QStringLiteral("Connection"), QStringLiteral("New Telnet Connection..."), {}, Scope::MainWindow},
        {QStringLiteral("connection.newSsh"), QStringLiteral("Connection"), QStringLiteral("New SSH Connection..."), {}, Scope::MainWindow},
        {QStringLiteral("connection.newLoopback"), QStringLiteral("Connection"), QStringLiteral("New Loopback (test)"), {}, Scope::MainWindow},
        {QStringLiteral("connection.closeTab"), QStringLiteral("Connection"), QStringLiteral("Close Pane / Tab"), seq("Ctrl+W"), Scope::MainWindow},

        {QStringLiteral("pane.splitRight"), QStringLiteral("Panes"), QStringLiteral("Split Pane Right"), seq("Ctrl+Shift+R"), Scope::MainWindow},
        {QStringLiteral("pane.splitDown"), QStringLiteral("Panes"), QStringLiteral("Split Pane Down"), seq("Ctrl+Shift+D"), Scope::MainWindow},
        {QStringLiteral("pane.close"), QStringLiteral("Panes"), QStringLiteral("Close Pane"), seq("Ctrl+Shift+W"), Scope::MainWindow},

        {QStringLiteral("terminal.find"), QStringLiteral("Terminal"), QStringLiteral("Find in Scrollback"), seq("Ctrl+Shift+F"), Scope::MainWindow},
        {QStringLiteral("terminal.copy"), QStringLiteral("Terminal"), QStringLiteral("Copy"), seq("Ctrl+Shift+C"), Scope::MainWindow},
        {QStringLiteral("terminal.paste"), QStringLiteral("Terminal"), QStringLiteral("Paste"), seq("Ctrl+Shift+V"), Scope::MainWindow},
        {QStringLiteral("terminal.clearBuffer"), QStringLiteral("Terminal"), QStringLiteral("Clear Buffer"), {}, Scope::MainWindow},
        {QStringLiteral("terminal.clearScreen"), QStringLiteral("Terminal"), QStringLiteral("Clear Screen"), {}, Scope::MainWindow},
        {QStringLiteral("terminal.log"), QStringLiteral("Terminal"), QStringLiteral("Log Session to File"), {}, Scope::MainWindow},

        {QStringLiteral("editor.save"), QStringLiteral("SFTP File Editor"), QStringLiteral("Save (upload)"), seq("Ctrl+S"), Scope::FileEditor},
        {QStringLiteral("editor.close"), QStringLiteral("SFTP File Editor"), QStringLiteral("Close Editor"), seq("Ctrl+W"), Scope::FileEditor},
        {QStringLiteral("editor.find"), QStringLiteral("SFTP File Editor"), QStringLiteral("Find"), seq("Ctrl+F"), Scope::FileEditor},
        {QStringLiteral("editor.findNext"), QStringLiteral("SFTP File Editor"), QStringLiteral("Find Next"), seq("F3"), Scope::FileEditor},
        {QStringLiteral("editor.findPrevious"), QStringLiteral("SFTP File Editor"), QStringLiteral("Find Previous"), seq("Shift+F3"), Scope::FileEditor},
    };
    return commands;
}
} // namespace

ShortcutManager &ShortcutManager::instance()
{
    static ShortcutManager instance;
    return instance;
}

// Same organisation/application/format as AppSettings, so it is the same
// ini file (QSettings instances on one file share their data in-process).
ShortcutManager::ShortcutManager() : m_settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lvdterm"), QStringLiteral("lvdterm"))
{
    m_settings.beginGroup(QLatin1String(kGroup));
    for (const ShortcutCommand &command : table()) {
        if (m_settings.contains(command.id))
            m_overrides.insert(command.id, firstChord(QKeySequence(asString(m_settings.value(command.id)), QKeySequence::PortableText)));
    }
    m_settings.endGroup();

    const QJsonArray array = QJsonDocument::fromJson(asString(m_settings.value(QLatin1String(kCustomKey))).toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        CustomShortcut shortcut;
        shortcut.id = object.value(QStringLiteral("id")).toString();
        if (!isCustomId(shortcut.id))
            continue;
        shortcut.name = object.value(QStringLiteral("name")).toString();
        shortcut.sequence = firstChord(QKeySequence(object.value(QStringLiteral("sequence")).toString(), QKeySequence::PortableText));
        shortcut.text = object.value(QStringLiteral("text")).toString();
        shortcut.appendEnter = object.value(QStringLiteral("appendEnter")).toBool();
        m_custom.append(shortcut);
    }
}

const QList<ShortcutCommand> &ShortcutManager::commands()
{
    return table();
}

const ShortcutCommand *ShortcutManager::command(const QString &id)
{
    for (const ShortcutCommand &command : table()) {
        if (command.id == id)
            return &command;
    }
    return nullptr;
}

QKeySequence ShortcutManager::sequence(const QString &id) const
{
    if (isCustomId(id)) {
        for (const CustomShortcut &shortcut : m_custom) {
            if (shortcut.id == id)
                return shortcut.sequence;
        }
        return {};
    }

    const auto it = m_overrides.constFind(id);
    if (it != m_overrides.constEnd())
        return it.value();
    const ShortcutCommand *c = command(id);
    return c ? c->defaultSequence : QKeySequence();
}

bool ShortcutManager::isCustomized(const QString &id) const
{
    return m_overrides.contains(id);
}

void ShortcutManager::setSequence(const QString &id, const QKeySequence &requested)
{
    const QKeySequence sequence = firstChord(requested);

    if (isCustomId(id)) {
        for (CustomShortcut &shortcut : m_custom) {
            if (shortcut.id == id && shortcut.sequence != sequence) {
                shortcut.sequence = sequence;
                saveCustom();
                emit shortcutsChanged();
            }
        }
        return;
    }

    const ShortcutCommand *c = command(id);
    if (!c || this->sequence(id) == sequence)
        return;

    m_settings.beginGroup(QLatin1String(kGroup));
    if (sequence == c->defaultSequence) {
        // Back on the default: forget the override, so a future change to
        // the default reaches this user too.
        m_overrides.remove(id);
        m_settings.remove(id);
    } else {
        m_overrides.insert(id, sequence);
        m_settings.setValue(id, toStored(sequence));
    }
    m_settings.endGroup();
    emit shortcutsChanged();
}

void ShortcutManager::resetToDefault(const QString &id)
{
    if (const ShortcutCommand *c = command(id))
        setSequence(id, c->defaultSequence);
}

void ShortcutManager::resetAllToDefaults()
{
    if (m_overrides.isEmpty())
        return;
    m_settings.beginGroup(QLatin1String(kGroup));
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        m_settings.remove(it.key());
    m_settings.endGroup();
    m_overrides.clear();
    emit shortcutsChanged();
}

void ShortcutManager::addCustom(const CustomShortcut &shortcut)
{
    m_custom.append(shortcut);
    m_custom.last().sequence = firstChord(shortcut.sequence);
    saveCustom();
    emit shortcutsChanged();
}

void ShortcutManager::updateCustom(const CustomShortcut &shortcut)
{
    for (CustomShortcut &existing : m_custom) {
        if (existing.id == shortcut.id) {
            existing = shortcut;
            existing.sequence = firstChord(shortcut.sequence);
            saveCustom();
            emit shortcutsChanged();
            return;
        }
    }
}

void ShortcutManager::removeCustom(const QString &id)
{
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom.at(i).id == id) {
            m_custom.removeAt(i);
            saveCustom();
            emit shortcutsChanged();
            return;
        }
    }
}

QString ShortcutManager::newCustomId()
{
    return QLatin1String(kCustomPrefix) + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool ShortcutManager::isCustomId(const QString &id)
{
    return id.startsWith(QLatin1String(kCustomPrefix));
}

ShortcutCommand::Scope ShortcutManager::scopeOf(const QString &id) const
{
    const ShortcutCommand *c = command(id);
    return c ? c->scope : ShortcutCommand::Scope::MainWindow; // custom shortcuts are main-window ones
}

QString ShortcutManager::conflictingCommand(const QString &id, const QKeySequence &sequence) const
{
    if (sequence.isEmpty())
        return {};

    const ShortcutCommand::Scope scope = scopeOf(id);
    for (const ShortcutCommand &c : table()) {
        if (c.id != id && c.scope == scope && this->sequence(c.id) == sequence)
            return c.id;
    }
    if (scope == ShortcutCommand::Scope::MainWindow) {
        for (const CustomShortcut &c : m_custom) {
            if (c.id != id && c.sequence == sequence)
                return c.id;
        }
    }
    return {};
}

QString ShortcutManager::labelFor(const QString &id) const
{
    if (const ShortcutCommand *c = command(id))
        return c->label;
    for (const CustomShortcut &c : m_custom) {
        if (c.id == id)
            return c.name;
    }
    return id;
}

bool ShortcutManager::isAcceptable(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return true; // "unbound" is always fine

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return true;

    const int key = combination.key();
    return key >= Qt::Key_F1 && key <= Qt::Key_F35;
}

QByteArray ShortcutManager::decodeText(const QString &text)
{
    QByteArray out;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char('\\') || i + 1 >= text.size()) {
            out += QString(ch).toUtf8();
            continue;
        }

        const QChar next = text.at(++i);
        switch (next.unicode()) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'e': out += '\x1b'; break;
        case '\\': out += '\\'; break;
        case 'x': {
            bool ok = false;
            const int value = i + 2 < text.size() ? text.mid(i + 1, 2).toInt(&ok, 16) : -1;
            if (ok) {
                out += static_cast<char>(value);
                i += 2;
            } else {
                out += "\\x";
            }
            break;
        }
        default:
            out += '\\';
            out += QString(next).toUtf8();
            break;
        }
    }
    return out;
}

void ShortcutManager::saveCustom()
{
    QJsonArray array;
    for (const CustomShortcut &c : m_custom) {
        array.append(QJsonObject{{QStringLiteral("id"), c.id},
                                 {QStringLiteral("name"), c.name},
                                 {QStringLiteral("sequence"), toStored(c.sequence)},
                                 {QStringLiteral("text"), c.text},
                                 {QStringLiteral("appendEnter"), c.appendEnter}});
    }
    m_settings.setValue(QLatin1String(kCustomKey), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}
