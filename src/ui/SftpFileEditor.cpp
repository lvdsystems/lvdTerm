#include "SftpFileEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QStringDecoder>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "sftp/SftpSession.h"
#include "ui/CodeEditor.h"

SftpFileEditor::SftpFileEditor(const SshConnectionSettings &settings, const QString &remotePath, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_settings(settings)
    , m_remotePath(remotePath)
    , m_session(new SftpSession(settings, this))
    , m_edit(new CodeEditor(this))
    , m_saveButton(new QPushButton(QStringLiteral("Save"), this))
    , m_status(new QLabel(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    resize(820, 600);

    m_localPath = m_tempDir.path() + QLatin1Char('/') + QFileInfo(remotePath).fileName();

    QFont font = AppSettings::instance().terminalFont();
    m_edit->setFont(font);
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_edit->setTabStopDistance(4 * QFontMetrics(font).horizontalAdvance(QLatin1Char(' ')));
    m_edit->setEnabled(false); // until the download finishes
    connect(m_edit->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });

    m_saveButton->setEnabled(false);
    m_saveButton->setToolTip(QStringLiteral("Upload the changes (Ctrl+S)"));
    connect(m_saveButton, &QPushButton::clicked, this, [this] { startSave(); });

    auto *path = new QLabel(QStringLiteral("%1@%2:%3").arg(settings.username, settings.host, remotePath), this);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto *top = new QHBoxLayout();
    top->addWidget(m_saveButton);
    top->addWidget(path, 1);

    // Find bar (Ctrl+F), hidden until asked for.
    m_findBar = new QWidget(this);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(QStringLiteral("Find"));
    m_findEdit->setClearButtonEnabled(true);
    m_findCase = new QCheckBox(QStringLiteral("Match case"), m_findBar);
    m_findCount = new QLabel(m_findBar);
    m_findCount->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_findCount->setMinimumWidth(QFontMetrics(m_findCount->font()).horizontalAdvance(QStringLiteral("5000+ matches")));
    auto *prevButton = new QToolButton(m_findBar);
    prevButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    prevButton->setToolTip(QStringLiteral("Previous match (Shift+Enter)"));
    auto *nextButton = new QToolButton(m_findBar);
    nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    nextButton->setToolTip(QStringLiteral("Next match (Enter)"));
    auto *closeFindButton = new QToolButton(m_findBar);
    closeFindButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeFindButton->setToolTip(QStringLiteral("Close (Esc)"));
    auto *findLayout = new QHBoxLayout(m_findBar);
    findLayout->setContentsMargins(0, 0, 0, 0);
    findLayout->addWidget(m_findEdit, 1);
    findLayout->addWidget(prevButton);
    findLayout->addWidget(nextButton);
    findLayout->addWidget(m_findCase);
    findLayout->addWidget(m_findCount);
    findLayout->addWidget(closeFindButton);
    m_findBar->hide();

    m_findRefresh = new QTimer(this);
    m_findRefresh->setSingleShot(true);
    m_findRefresh->setInterval(150);
    connect(m_findRefresh, &QTimer::timeout, this, [this] { refreshFindHighlights(false); });
    connect(m_edit->document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_findBar->isVisible())
            m_findRefresh->start();
    });
    connect(m_findEdit, &QLineEdit::textChanged, this, [this] { refreshFindHighlights(true); });
    connect(m_findCase, &QCheckBox::toggled, this, [this] { refreshFindHighlights(true); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { findStep(QApplication::keyboardModifiers() & Qt::ShiftModifier); });
    connect(nextButton, &QToolButton::clicked, this, [this] { findStep(false); });
    connect(prevButton, &QToolButton::clicked, this, [this] { findStep(true); });
    connect(closeFindButton, &QToolButton::clicked, this, &SftpFileEditor::hideFindBar);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_findBar);
    layout->addWidget(m_status);

    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { startSave(); });
    auto *closeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);

    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &SftpFileEditor::showFindBar);
    auto *findNext = new QShortcut(QKeySequence::FindNext, this);
    findNext->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findNext, &QShortcut::activated, this, [this] { findStep(false); });
    auto *findPrevious = new QShortcut(QKeySequence::FindPrevious, this);
    findPrevious->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findPrevious, &QShortcut::activated, this, [this] { findStep(true); });
    m_escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    m_escapeShortcut->setEnabled(false); // only while the find bar is open
    connect(m_escapeShortcut, &QShortcut::activated, this, &SftpFileEditor::hideFindBar);

    connect(m_session, &SftpSession::connected, this, &SftpFileEditor::onConnected);
    connect(m_session, &SftpSession::connectFailed, this, [this](const QString &message) { setStatus(QStringLiteral("Connection failed: %1").arg(message)); });
    connect(m_session, &SftpSession::operationSucceeded, this, &SftpFileEditor::onOperationSucceeded);
    connect(m_session, &SftpSession::operationFailed, this, &SftpFileEditor::onOperationFailed);
    connect(m_session, &SftpSession::transferProgress, this, [this](const QString &path, qint64 done, qint64 total) {
        if (path == m_remotePath && total > 0)
            setStatus(QStringLiteral("%1 %2%...").arg(m_loaded ? QStringLiteral("Saving") : QStringLiteral("Downloading")).arg(done * 100 / total));
    });

    updateTitle();
    setStatus(QStringLiteral("Connecting..."));
    m_session->connectToHost();
}

SftpFileEditor::~SftpFileEditor()
{
    // The temp folder (and the working copy in it) is removed by
    // m_tempDir's own destructor; the connection by SftpSession's.
    m_session->disconnectFromHost();
}

void SftpFileEditor::onConnected()
{
    setStatus(QStringLiteral("Downloading..."));
    m_session->downloadFile(m_remotePath, m_localPath);
}

void SftpFileEditor::onOperationSucceeded(const QString &operation, const QString &path)
{
    if (path != m_remotePath)
        return;

    if (operation == QStringLiteral("download") && !m_loaded) {
        loadDownloadedFile();
    } else if (operation == QStringLiteral("upload") && m_saving) {
        m_saving = false;
        m_edit->document()->setModified(false);
        m_saveButton->setEnabled(false);
        setStatus(QStringLiteral("Saved %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
        m_lastSaveOk = true;
        emit saved(m_remotePath);
        emit saveFinished(true);
    }
}

void SftpFileEditor::onOperationFailed(const QString &operation, const QString &path, const QString &message)
{
    if (path != m_remotePath)
        return;

    if (operation == QStringLiteral("upload") && m_saving) {
        m_saving = false;
        m_saveButton->setEnabled(true);
        setStatus(QStringLiteral("Save failed: %1").arg(message));
        QMessageBox::warning(this, QStringLiteral("Save Failed"), QStringLiteral("Could not upload %1:\n%2").arg(m_remotePath, message));
        m_lastSaveOk = false;
        emit saveFinished(false);
    } else if (operation == QStringLiteral("download") && !m_loaded) {
        setStatus(QStringLiteral("Could not download the file: %1").arg(message));
    }
}

void SftpFileEditor::loadDownloadedFile()
{
    QFile file(m_localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Could not read the downloaded copy: %1").arg(file.errorString()));
        return;
    }
    QByteArray bytes = file.readAll();
    file.close();

    if (bytes.size() > kMaxBytes) {
        setStatus(QStringLiteral("Too large to edit here - use Download instead."));
        return;
    }
    // A NUL byte in the first chunk is the usual tell for a binary file.
    if (bytes.left(8192).contains('\0')) {
        setStatus(QStringLiteral("This looks like a binary file - not opened. Use Download instead."));
        return;
    }

    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder(bytes);
    if (decoder.hasError()) {
        // Saving would re-encode as UTF-8 and silently change the bytes.
        setStatus(QStringLiteral("Not valid UTF-8 text - not opened (editing it here could corrupt it). Use Download instead."));
        return;
    }

    m_crlf = bytes.contains("\r\n");
    QString normalized = text;
    if (m_crlf)
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    m_language = m_edit->setSyntax(QFileInfo(m_remotePath).fileName(), normalized.section(QLatin1Char('\n'), 0, 0));
    m_edit->setPlainText(normalized);
    m_edit->document()->setModified(false);
    m_edit->setEnabled(true);
    m_edit->setFocus();
    m_loaded = true;
    setStatus(QStringLiteral("%1 - %2 lines - Ctrl+S to save, Ctrl+F to find").arg(m_language).arg(m_edit->document()->blockCount()));
    updateTitle();
}

bool SftpFileEditor::startSave()
{
    if (!m_loaded || m_saving)
        return false;

    QString text = m_edit->toPlainText();
    if (m_crlf)
        text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    QFile file(m_localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.toUtf8()) < 0) {
        setStatus(QStringLiteral("Could not write the working copy: %1").arg(file.errorString()));
        return false;
    }
    file.close();

    m_saving = true;
    m_saveButton->setEnabled(false);
    setStatus(QStringLiteral("Saving..."));
    m_session->uploadFile(m_localPath, m_remotePath);
    return true;
}

bool SftpFileEditor::saveAndWait()
{
    if (!startSave())
        return false;

    QEventLoop loop;
    connect(this, &SftpFileEditor::saveFinished, &loop, &QEventLoop::quit);
    loop.exec();
    return m_lastSaveOk;
}

bool SftpFileEditor::requestClose()
{
    if (!m_loaded || !m_edit->document()->isModified())
        return true;

    const auto choice = QMessageBox::question(
        this, QStringLiteral("Unsaved Changes"), QStringLiteral("Save changes to %1 before closing?").arg(QFileInfo(m_remotePath).fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (choice == QMessageBox::Save)
        return saveAndWait();
    return choice == QMessageBox::Discard;
}

void SftpFileEditor::closeEvent(QCloseEvent *event)
{
    if (requestClose())
        event->accept();
    else
        event->ignore();
}

void SftpFileEditor::showFindBar()
{
    // Start from the selected text, as most editors do.
    const QString selected = m_edit->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selected);
    m_findBar->show();
    m_escapeShortcut->setEnabled(true);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    refreshFindHighlights(false);
}

void SftpFileEditor::hideFindBar()
{
    m_findBar->hide();
    m_escapeShortcut->setEnabled(false);
    m_edit->setSearchTerm(QString(), false);
    m_edit->setFocus();
}

void SftpFileEditor::findStep(bool backwards)
{
    if (!m_findBar->isVisible()) {
        showFindBar();
        return;
    }
    if (!m_edit->findText(m_findEdit->text(), m_findCase->isChecked(), backwards))
        refreshFindHighlights(false);
}

void SftpFileEditor::refreshFindHighlights(bool selectMatch)
{
    const QString term = m_findEdit->text();
    const bool cs = m_findCase->isChecked();
    const int count = m_edit->setSearchTerm(term, cs);
    if (selectMatch)
        m_edit->findText(term, cs, false, true);

    const bool missing = !term.isEmpty() && count == 0;
    m_findEdit->setStyleSheet(missing ? QStringLiteral("QLineEdit { border: 1px solid #d9534f; }") : QString());
    if (term.isEmpty())
        m_findCount->clear();
    else if (missing)
        m_findCount->setText(QStringLiteral("No results"));
    else
        m_findCount->setText(count >= CodeEditor::kMaxHighlightedMatches ? QStringLiteral("%1+ matches").arg(count) : (count == 1 ? QStringLiteral("1 match") : QStringLiteral("%1 matches").arg(count)));
}

void SftpFileEditor::setStatus(const QString &text)
{
    m_status->setText(text);
}

void SftpFileEditor::updateTitle()
{
    const bool modified = m_loaded && m_edit->document()->isModified();
    setWindowTitle(QStringLiteral("%1%2 - %3@%4").arg(QFileInfo(m_remotePath).fileName(), modified ? QStringLiteral(" *") : QString(), m_settings.username, m_settings.host));
    m_saveButton->setEnabled(modified && !m_saving);
}
