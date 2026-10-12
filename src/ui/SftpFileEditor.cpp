#include "SftpFileEditor.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStringDecoder>
#include <QVBoxLayout>

#include "core/AppSettings.h"
#include "sftp/SftpSession.h"

SftpFileEditor::SftpFileEditor(const SshConnectionSettings &settings, const QString &remotePath, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_settings(settings)
    , m_remotePath(remotePath)
    , m_session(new SftpSession(settings, this))
    , m_edit(new QPlainTextEdit(this))
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

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_status);

    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { startSave(); });
    auto *closeShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut, &QShortcut::activated, this, &QWidget::close);

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

    m_edit->setPlainText(normalized);
    m_edit->document()->setModified(false);
    m_edit->setEnabled(true);
    m_edit->setFocus();
    m_loaded = true;
    setStatus(QStringLiteral("%1 lines - Ctrl+S to save").arg(m_edit->document()->blockCount()));
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
