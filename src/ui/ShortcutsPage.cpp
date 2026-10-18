#include "ShortcutsPage.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/ShortcutManager.h"

namespace
{
constexpr int kIdRole = Qt::UserRole;

const char *const kAcceptableHint = "A shortcut needs Ctrl, Alt or Meta (or be a function key) - a plain letter would stop you typing.";

QKeySequence firstChord(const QKeySequence &sequence)
{
    return sequence.isEmpty() ? QKeySequence() : QKeySequence(sequence[0]);
}

// A key box that keeps a single chord: QKeySequenceEdit records up to four.
QKeySequenceEdit *makeKeyEdit(QWidget *parent, const QKeySequence &initial)
{
    auto *edit = new QKeySequenceEdit(initial, parent);
    QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, edit, [edit](const QKeySequence &sequence) {
        if (sequence.count() > 1)
            edit->setKeySequence(firstChord(sequence));
    });
    return edit;
}

// "Press the new shortcut" prompt. Returns false if cancelled.
bool captureSequence(QWidget *parent, const QString &commandLabel, const QKeySequence &current, QKeySequence *result)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Change Shortcut"));

    auto *edit = makeKeyEdit(&dialog, current);
    auto *clearButton = new QPushButton(QStringLiteral("Clear"), &dialog);
    QObject::connect(clearButton, &QPushButton::clicked, edit, &QKeySequenceEdit::clear);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *row = new QHBoxLayout();
    row->addWidget(edit, 1);
    row->addWidget(clearButton);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Press the new shortcut for <b>%1</b>:").arg(commandLabel.toHtmlEscaped()), &dialog));
    layout->addLayout(row);
    layout->addWidget(buttons);
    edit->setFocus();

    if (dialog.exec() != QDialog::Accepted)
        return false;
    *result = firstChord(edit->keySequence());
    return true;
}

// Name + key + text for a custom shortcut. Returns false if cancelled.
bool editCustom(QWidget *parent, CustomShortcut *shortcut)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(shortcut->name.isEmpty() ? QStringLiteral("New Custom Shortcut") : QStringLiteral("Edit Custom Shortcut"));
    dialog.setMinimumWidth(420);

    auto *nameEdit = new QLineEdit(shortcut->name, &dialog);
    nameEdit->setPlaceholderText(QStringLiteral("e.g. List files"));
    auto *keyEdit = makeKeyEdit(&dialog, shortcut->sequence);
    auto *textEdit = new QLineEdit(shortcut->text, &dialog);
    textEdit->setPlaceholderText(QStringLiteral("e.g. ls -la"));
    textEdit->setToolTip(QStringLiteral("Typed into the active terminal. Escapes: \\n \\r \\t \\e (Esc) \\\\ \\xHH (a byte, e.g. \\x03 = Ctrl+C)."));
    auto *enterCheck = new QCheckBox(QStringLiteral("Press Enter after the text"), &dialog);
    enterCheck->setChecked(shortcut->appendEnter);

    auto *hint = new QLabel(QStringLiteral("Types the text into the active terminal. Escapes: \\n \\t \\e \\\\ \\xHH (e.g. \\x03 is Ctrl+C)."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (nameEdit->text().trimmed().isEmpty() || textEdit->text().isEmpty() || keyEdit->keySequence().isEmpty()) {
            QMessageBox::information(&dialog, dialog.windowTitle(), QStringLiteral("Give it a name, a shortcut and the text to send."));
            return;
        }
        if (!ShortcutManager::isAcceptable(keyEdit->keySequence())) {
            QMessageBox::information(&dialog, dialog.windowTitle(), QString::fromLatin1(kAcceptableHint));
            return;
        }
        dialog.accept();
    });

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Name:"), nameEdit);
    form->addRow(QStringLiteral("Shortcut:"), keyEdit);
    form->addRow(QStringLiteral("Text to send:"), textEdit);
    form->addRow(QString(), enterCheck);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
    nameEdit->setFocus();

    if (dialog.exec() != QDialog::Accepted)
        return false;
    shortcut->name = nameEdit->text().trimmed();
    shortcut->sequence = firstChord(keyEdit->keySequence());
    shortcut->text = textEdit->text();
    shortcut->appendEnter = enterCheck->isChecked();
    return true;
}
} // namespace

ShortcutsPage::ShortcutsPage(QWidget *parent)
    : QWidget(parent)
    , m_filterEdit(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
    , m_changeButton(new QPushButton(QStringLiteral("Change..."), this))
    , m_clearButton(new QPushButton(QStringLiteral("Clear"), this))
    , m_resetButton(new QPushButton(QStringLiteral("Reset"), this))
    , m_editCustomButton(new QPushButton(QStringLiteral("Edit..."), this))
    , m_deleteCustomButton(new QPushButton(QStringLiteral("Delete"), this))
{
    m_filterEdit->setPlaceholderText(QStringLiteral("Search commands or shortcuts..."));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &ShortcutsPage::applyFilter);

    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ShortcutsPage::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { activated(item); });

    m_changeButton->setToolTip(QStringLiteral("Record a new key for the selected command"));
    m_clearButton->setToolTip(QStringLiteral("Leave the selected command without a shortcut"));
    m_resetButton->setToolTip(QStringLiteral("Back to the built-in shortcut"));
    connect(m_changeButton, &QPushButton::clicked, this, &ShortcutsPage::changeSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &ShortcutsPage::clearSelected);
    connect(m_resetButton, &QPushButton::clicked, this, &ShortcutsPage::resetSelected);
    connect(m_editCustomButton, &QPushButton::clicked, this, &ShortcutsPage::editSelectedCustom);
    connect(m_deleteCustomButton, &QPushButton::clicked, this, &ShortcutsPage::deleteSelectedCustom);

    auto *addButton = new QPushButton(QStringLiteral("Add Custom..."), this);
    addButton->setToolTip(QStringLiteral("A new shortcut that types a piece of text into the active terminal"));
    connect(addButton, &QPushButton::clicked, this, &ShortcutsPage::addCustom);
    auto *resetAllButton = new QPushButton(QStringLiteral("Reset All"), this);
    resetAllButton->setToolTip(QStringLiteral("Put every built-in command back to its default shortcut (custom shortcuts are kept)"));
    connect(resetAllButton, &QPushButton::clicked, this, &ShortcutsPage::resetAll);

    auto *editRow = new QHBoxLayout();
    editRow->addWidget(m_changeButton);
    editRow->addWidget(m_clearButton);
    editRow->addWidget(m_resetButton);
    editRow->addSpacing(12);
    editRow->addWidget(addButton);
    editRow->addWidget(m_editCustomButton);
    editRow->addWidget(m_deleteCustomButton);
    editRow->addStretch();
    editRow->addWidget(resetAllButton);

    auto *hint = new QLabel(QStringLiteral("Double-click a command to change its shortcut. Changes apply immediately."), this);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_filterEdit);
    layout->addWidget(m_tree, 1);
    layout->addLayout(editRow);
    layout->addWidget(hint);

    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &ShortcutsPage::rebuild);
    rebuild();
}

void ShortcutsPage::rebuild()
{
    const QString keepSelected = selectedId();
    const ShortcutManager &manager = ShortcutManager::instance();

    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> categories;
    auto categoryItem = [&](const QString &name) {
        QTreeWidgetItem *&item = categories[name];
        if (!item) {
            item = new QTreeWidgetItem(m_tree, {name});
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setFirstColumnSpanned(true);
            item->setFlags(Qt::ItemIsEnabled); // a heading: not selectable
        }
        return item;
    };

    auto addRow = [&](const QString &category, const QString &id, const QString &label, const QKeySequence &sequence, bool modified) {
        auto *item = new QTreeWidgetItem(categoryItem(category));
        item->setData(0, kIdRole, id);
        item->setText(0, modified ? label + QStringLiteral("  (modified)") : label);
        if (sequence.isEmpty()) {
            item->setText(1, QStringLiteral("Not set"));
            item->setForeground(1, palette().color(QPalette::Disabled, QPalette::Text));
        } else {
            item->setText(1, sequence.toString(QKeySequence::NativeText));
        }
        return item;
    };

    for (const ShortcutCommand &command : ShortcutManager::commands())
        addRow(command.category, command.id, command.label, manager.sequence(command.id), manager.isCustomized(command.id));

    // Custom shortcuts get a category of their own, last.
    const QString customCategory = QStringLiteral("Custom (send text)");
    for (const CustomShortcut &custom : manager.customShortcuts()) {
        QTreeWidgetItem *item = addRow(customCategory, custom.id, custom.name, custom.sequence, false);
        item->setToolTip(0, QStringLiteral("Sends: %1%2").arg(custom.text, custom.appendEnter ? QStringLiteral("  + Enter") : QString()));
    }

    m_tree->expandAll();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *category = m_tree->topLevelItem(i);
        for (int j = 0; j < category->childCount(); ++j) {
            if (category->child(j)->data(0, kIdRole).toString() == keepSelected)
                m_tree->setCurrentItem(category->child(j));
        }
    }
    applyFilter();
    updateButtons();
}

void ShortcutsPage::applyFilter()
{
    const QString needle = m_filterEdit->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *category = m_tree->topLevelItem(i);
        bool anyVisible = false;
        for (int j = 0; j < category->childCount(); ++j) {
            QTreeWidgetItem *item = category->child(j);
            const bool match = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive) || item->text(1).contains(needle, Qt::CaseInsensitive)
                               || category->text(0).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            anyVisible = anyVisible || match;
        }
        category->setHidden(!anyVisible);
    }
}

QString ShortcutsPage::selectedId() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

void ShortcutsPage::updateButtons()
{
    const QString id = selectedId();
    const bool any = !id.isEmpty();
    const bool custom = ShortcutManager::isCustomId(id);
    const ShortcutManager &manager = ShortcutManager::instance();

    m_changeButton->setEnabled(any);
    m_clearButton->setEnabled(any && !manager.sequence(id).isEmpty());
    m_resetButton->setEnabled(any && !custom && manager.isCustomized(id));
    m_editCustomButton->setEnabled(custom);
    m_deleteCustomButton->setEnabled(custom);
}

bool ShortcutsPage::assign(const QString &id, const QKeySequence &sequence)
{
    ShortcutManager &manager = ShortcutManager::instance();

    if (!ShortcutManager::isAcceptable(sequence)) {
        QMessageBox::information(this, QStringLiteral("Keyboard Shortcuts"), QString::fromLatin1(kAcceptableHint));
        return false;
    }

    const QString other = manager.conflictingCommand(id, sequence);
    if (!other.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Shortcut already in use"),
            QStringLiteral("%1 is already used by \"%2\".\n\nUse it for \"%3\" instead? \"%2\" will have no shortcut.")
                .arg(sequence.toString(QKeySequence::NativeText), manager.labelFor(other), manager.labelFor(id)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return false;
        manager.setSequence(other, QKeySequence());
    }

    manager.setSequence(id, sequence);
    return true;
}

void ShortcutsPage::changeSelected()
{
    const QString id = selectedId();
    if (id.isEmpty())
        return;

    const ShortcutManager &manager = ShortcutManager::instance();
    QKeySequence chosen;
    if (captureSequence(this, manager.labelFor(id), manager.sequence(id), &chosen))
        assign(id, chosen);
}

void ShortcutsPage::clearSelected()
{
    const QString id = selectedId();
    if (!id.isEmpty())
        ShortcutManager::instance().setSequence(id, QKeySequence());
}

void ShortcutsPage::resetSelected()
{
    const QString id = selectedId();
    const ShortcutCommand *command = ShortcutManager::command(id);
    if (command)
        assign(id, command->defaultSequence);
}

void ShortcutsPage::resetAll()
{
    if (QMessageBox::question(this, QStringLiteral("Reset All Shortcuts"),
                              QStringLiteral("Put every built-in command back to its default shortcut?\nYour custom shortcuts are kept."),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;

    ShortcutManager &manager = ShortcutManager::instance();
    manager.resetAllToDefaults();

    // A default may now collide with a custom shortcut (two shortcuts on one
    // key cancel each other out) - the built-in wins, the custom one is unbound.
    QStringList unbound;
    for (const CustomShortcut &custom : manager.customShortcuts()) {
        if (!manager.conflictingCommand(custom.id, custom.sequence).isEmpty()) {
            manager.setSequence(custom.id, QKeySequence());
            unbound << custom.name;
        }
    }
    if (!unbound.isEmpty())
        QMessageBox::information(this, QStringLiteral("Reset All Shortcuts"),
                                 QStringLiteral("These custom shortcuts used a key that is a default again, so they were cleared:\n\n%1").arg(unbound.join(QLatin1Char('\n'))));
}

void ShortcutsPage::addCustom()
{
    ShortcutManager &manager = ShortcutManager::instance();

    CustomShortcut shortcut;
    shortcut.id = ShortcutManager::newCustomId();
    if (!editCustom(this, &shortcut))
        return;

    // Add it without a key first, then claim the key through assign() so a
    // clash is asked about like any other change; backing out of that
    // drops the new shortcut again.
    const QKeySequence wanted = shortcut.sequence;
    shortcut.sequence = QKeySequence();
    manager.addCustom(shortcut);
    if (!assign(shortcut.id, wanted))
        manager.removeCustom(shortcut.id);
}

void ShortcutsPage::editSelectedCustom()
{
    ShortcutManager &manager = ShortcutManager::instance();
    const QString id = selectedId();
    for (CustomShortcut shortcut : manager.customShortcuts()) {
        if (shortcut.id != id)
            continue;

        const QKeySequence before = shortcut.sequence;
        if (!editCustom(this, &shortcut))
            return;

        const QKeySequence wanted = shortcut.sequence;
        shortcut.sequence = before; // the key changes below, via assign()
        manager.updateCustom(shortcut);
        if (wanted != before && !assign(id, wanted))
            manager.setSequence(id, before);
        return;
    }
}

void ShortcutsPage::deleteSelectedCustom()
{
    const QString id = selectedId();
    if (!ShortcutManager::isCustomId(id))
        return;

    ShortcutManager &manager = ShortcutManager::instance();
    if (QMessageBox::question(this, QStringLiteral("Delete Custom Shortcut"), QStringLiteral("Delete \"%1\"?").arg(manager.labelFor(id)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        == QMessageBox::Yes)
        manager.removeCustom(id);
}

void ShortcutsPage::activated(QTreeWidgetItem *item)
{
    if (!item || item->data(0, kIdRole).toString().isEmpty())
        return;
    if (ShortcutManager::isCustomId(item->data(0, kIdRole).toString()))
        editSelectedCustom();
    else
        changeSelected();
}
