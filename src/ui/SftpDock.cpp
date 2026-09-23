#include "SftpDock.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include "sftp/SftpSession.h"

namespace
{
QString joinRemotePath(const QString &dir, const QString &name)
{
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

// QStyle::SP_FileDialogToParent (the "correct" standard icon for this)
// rendered close enough to SP_ArrowUp (Upload's icon) under Fusion that
// the two were reported as hard to tell apart. Drawn by hand instead - a
// chevron over a floor/bar, the conventional "go up a level" glyph (distinct
// in *shape*, not just which way it points, from Upload's plain arrow).
QIcon makeUpLevelIcon()
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QApplication::palette().color(QPalette::ButtonText));
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawPolyline(QPolygonF{QPointF(3, 9), QPointF(8, 4), QPointF(13, 9)});
    painter.drawLine(QPointF(3, 13), QPointF(13, 13));
    return QIcon(pixmap);
}
} // namespace

// A single-folder, Explorer-style listing (not a tree - see SftpDock.h)
// that knows how to drag remote files out to Explorer (downloading them
// to a temp file first - blocking briefly, see SftpDock.h) and accept
// files dragged in from Explorer (upload). Defined here rather than in
// its own header since nothing outside this file needs it; AUTOMOC still
// picks it up via the #include "SftpDock.moc" at the end of this file.
class SftpBrowserView : public QTableView
{
    Q_OBJECT

public:
    explicit SftpBrowserView(QWidget *parent = nullptr) : QTableView(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setShowGrid(false);
        verticalHeader()->setVisible(false);
    }

    void setSftpModel(SftpModel *model) { m_model = model; }
    void setSession(SftpSession *session) { m_session = session; }

signals:
    void uploadRequested(const QString &localPath, const QString &remotePath);
    void dragOutTooLarge(); // selection wasn't a single small file - see startDrag()

protected:
    // QAbstractItemView only ever starts an item drag from a press that
    // landed on an already-selected row; a press on a row that wasn't
    // selected yet falls through to ExtendedSelection's *other* built-in
    // click-and-drag behavior instead - extending the selection to every
    // row the mouse passes over (this is documented Qt behavior, not a
    // bug in the base class) - which is exactly what was reported:
    // holding the button down and moving selects multiple rows instead
    // of dragging the one under the cursor out. Pre-selecting a plain
    // left-click (no Ctrl/Shift) on a not-yet-selected row, before the
    // base class ever sees the press, makes that same first click-drag
    // start a real item drag instead, matching Explorer.
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            const QModelIndex index = indexAt(event->pos());
            if (index.isValid() && selectionModel() && !selectionModel()->isSelected(index))
                selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        QTableView::mousePressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasUrls() || !m_model || !m_session) {
            event->ignore();
            return;
        }

        const QModelIndex dropIndex = indexAt(event->position().toPoint());
        QString targetDir = (dropIndex.isValid() && m_model->isDirectory(dropIndex)) ? m_model->pathForIndex(dropIndex) : m_model->pathForIndex(rootIndex());

        for (const QUrl &url : event->mimeData()->urls()) {
            if (!url.isLocalFile())
                continue;
            const QFileInfo info(url.toLocalFile());
            // Directories are uploaded recursively - onUploadRequested()
            // dispatches to uploadFile()/uploadDirectory() based on
            // whether localPath is a file or a directory.
            emit uploadRequested(info.absoluteFilePath(), joinRemotePath(targetDir, info.fileName()));
        }

        event->acceptProposedAction();
    }

    // QTableView::startDrag() would use the model's own mimeData(), which
    // we don't implement (see SftpModel) - drag-out needs to actually
    // download the file first, which only this view (with a live
    // SftpSession) can do, and Qt's QDrag/QMimeData API needs that
    // download to have *already happened* before the OS drag can even
    // start (there's no cross-platform way to hand Explorer bytes lazily,
    // only once it's actually dropped somewhere - that needs a native
    // Windows OLE IDataObject with delayed rendering, a substantial
    // separate feature, not attempted here).
    //
    // Blocking the GUI thread to eagerly download was reported as feeling
    // like a crash/freeze, worse the bigger the transfer - and for a
    // folder or a large file, it also means writing a full temporary copy
    // to disk that's only ever needed if the user actually completes the
    // drag (most of which is wasted work if they don't). So eager
    // temp-file drag-out is only offered for a single small file, where
    // the wait is negligible either way; anything bigger is redirected to
    // the toolbar's "Download..." (downloadSelected()), which is already
    // fully non-blocking and streams straight to a chosen destination
    // with no temp file at all.
    void startDrag(Qt::DropActions supportedActions) override
    {
        Q_UNUSED(supportedActions);
        if (!m_model || !m_session)
            return;

        const QModelIndexList selected = selectionModel()->selectedRows(SftpModel::NameColumn);
        constexpr qint64 kMaxEagerDragBytes = 5 * 1024 * 1024; // a few MB transfers near-instantly on any real link
        const bool eligible = selected.size() == 1 && !m_model->isDirectory(selected.first())
                            && m_model->sizeOf(selected.first()) <= kMaxEagerDragBytes;
        if (!eligible) {
            emit dragOutTooLarge();
            return;
        }
        const QModelIndex index = selected.first();

        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        bool cursorRestored = false;
        struct CursorGuard
        {
            bool &restored;
            ~CursorGuard()
            {
                if (!restored)
                    QGuiApplication::restoreOverrideCursor();
            }
        } cursorGuard{cursorRestored};

        const QString tempDir = QDir::tempPath() + QStringLiteral("/lvdterm-sftp-drag");
        QDir().mkpath(tempDir);

        const QString remotePath = m_model->pathForIndex(index);
        const QString localPath = tempDir + QLatin1Char('/') + m_model->nameOf(index);

        bool ok = false;
        QEventLoop loop;
        QMetaObject::Connection okConn =
            connect(m_session, &SftpSession::operationSucceeded, &loop, [&](const QString &operation, const QString &path) {
                if (operation == QStringLiteral("download") && path == remotePath) {
                    ok = true;
                    loop.quit();
                }
            });
        QMetaObject::Connection failConn =
            connect(m_session, &SftpSession::operationFailed, &loop, [&](const QString &operation, const QString &path, const QString &) {
                if (operation == QStringLiteral("download") && path == remotePath)
                    loop.quit();
            });
        // If the active connection changes mid-drag (SftpDock tears this
        // session down), nothing would ever quit the loop otherwise - it
        // would hang forever instead of just failing this one drag.
        QMetaObject::Connection destroyedConn = connect(m_session, &QObject::destroyed, &loop, &QEventLoop::quit);

        m_session->downloadFile(remotePath, localPath);
        loop.exec();
        disconnect(okConn);
        disconnect(failConn);
        disconnect(destroyedConn);

        if (!ok)
            return;

        QGuiApplication::restoreOverrideCursor(); // download's done - let QDrag::exec() show its own drag cursor, not a wait cursor
        cursorRestored = true;

        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(localPath)});
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }

private:
    SftpModel *m_model = nullptr;
    SftpSession *m_session = nullptr;
};

SftpDock::SftpDock(QWidget *parent)
    : QDockWidget(QStringLiteral("SFTP"), parent)
    , m_model(new SftpModel(this))
    , m_view(new SftpBrowserView(this))
    , m_pathLabel(new QLabel(this))
    , m_statusLabel(new QLabel(QStringLiteral("Not connected"), this))
    , m_progressBar(new QProgressBar(this))
{
    setObjectName(QStringLiteral("SftpDock"));

    m_view->setModel(m_model);
    m_view->setSftpModel(m_model);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTableView::doubleClicked, this, &SftpDock::onItemDoubleClicked);
    connect(m_view, &QTableView::customContextMenuRequested, this, &SftpDock::showItemContextMenu);
    connect(m_view, &SftpBrowserView::dragOutTooLarge, this,
            [this] { setStatus(QStringLiteral("Too large to drag out directly - use \"Download...\" instead")); });

    // Explorer-details-view look, with columns the user can actually
    // resize by dragging (reported directly: they couldn't before -
    // Stretch/ResizeToContents both lock a column against manual
    // resizing, that's the whole point of those modes, not a fixed-width
    // quirk to work around). Interactive on every column plus explicit
    // starting widths gives the same "Name gets the room" initial look
    // without giving up drag-to-resize on any of them.
    m_view->setAlternatingRowColors(true);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(false); // otherwise Size, the last column, would stay locked
    m_view->setColumnWidth(SftpModel::NameColumn, 240);
    m_view->setColumnWidth(SftpModel::ModifiedColumn, 140);
    m_view->setColumnWidth(SftpModel::TypeColumn, 90);
    m_view->setColumnWidth(SftpModel::SizeColumn, 80);

    m_progressBar->setVisible(false);
    m_progressBar->setRange(0, 100);

    m_pathLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); // path is worth being able to copy

    m_toolbar = new QToolBar(this);
    auto *toolbar = m_toolbar;
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Icons over labels (QStyle::standardIcon - no custom art needed), with
    // the original label kept as a tooltip so nothing is less discoverable.
    auto addToolAction = [&](QStyle::StandardPixmap icon, const QString &tooltip, auto slot) {
        QAction *action = toolbar->addAction(style()->standardIcon(icon), QString());
        action->setToolTip(tooltip);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    m_upAction = toolbar->addAction(makeUpLevelIcon(), QString());
    m_upAction->setToolTip(QStringLiteral("Up"));
    connect(m_upAction, &QAction::triggered, this, &SftpDock::goUp);
    m_upAction->setEnabled(false);
    toolbar->addSeparator();
    addToolAction(QStyle::SP_ArrowUp, QStringLiteral("Upload..."), &SftpDock::uploadFiles);
    addToolAction(QStyle::SP_DirOpenIcon, QStringLiteral("Upload Folder..."), &SftpDock::uploadFolder);
    addToolAction(QStyle::SP_ArrowDown, QStringLiteral("Download..."), &SftpDock::downloadSelected);
    addToolAction(QStyle::SP_FileDialogNewFolder, QStringLiteral("New Folder..."), &SftpDock::newFolder);
    // No good standard icon for rename - a text label stays clearer than a
    // misleading icon.
    connect(toolbar->addAction(QStringLiteral("Rename...")), &QAction::triggered, this, &SftpDock::renameSelected);
    addToolAction(QStyle::SP_TrashIcon, QStringLiteral("Delete"), &SftpDock::deleteSelected);
    addToolAction(QStyle::SP_BrowserReload, QStringLiteral("Refresh"), &SftpDock::refresh);

    auto *pathBar = new QWidget(this);
    auto *pathLayout = new QHBoxLayout(pathBar);
    pathLayout->setContentsMargins(6, 2, 6, 2);
    pathLayout->addWidget(m_pathLabel, 1);

    // A ".." entry directly above the listing, styled to read as part of
    // it (same icon size, left-aligned like a row) - requested directly
    // as an additional, more discoverable way to go up than the toolbar
    // button alone. A real QPushButton rather than an actual model row:
    // making SftpModel itself understand a synthetic ".." row would mean
    // every index()/rowCount()/parent() implementation - and every place
    // a QModelIndex's identity is compared - has to agree on what a
    // "fake" index means, for one button's worth of functionality: not
    // worth the risk of a subtle model/view consistency bug. This calls
    // the exact same goUp() the toolbar button does.
    m_dotDotButton = new QPushButton(QStringLiteral(" .."), this);
    m_dotDotButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    m_dotDotButton->setFlat(true);
    // Explicit palette-based color: without it, the style sheet engine's
    // own default (opaque black) text color won - invisible against this
    // app's dark theme (found via a screenshot: the row rendered as a
    // blank strip, not a rendering-size problem at all).
    m_dotDotButton->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 4px 8px; border: none; color: palette(text); } "
                                                  "QPushButton:hover { background: palette(alternate-base); }"));
    m_dotDotButton->setVisible(false);
    connect(m_dotDotButton, &QPushButton::clicked, this, &SftpDock::goUp);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(pathBar);
    layout->addWidget(m_dotDotButton);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_progressBar);
    setWidget(container);

    // No SSH pane is focused yet at startup - see setActiveConnection().
    m_view->setEnabled(false);
    m_toolbar->setEnabled(false);
}

SftpDock::~SftpDock()
{
    teardownSession();
}

void SftpDock::teardownSession()
{
    if (m_session) {
        m_session->disconnectFromHost();
        m_session->deleteLater();
        m_session = nullptr;
    }
    m_view->setSession(nullptr);
}

void SftpDock::setActiveConnection(const std::optional<SshConnectionSettings> &settings)
{
    // Same host+credentials already in use for that pane -> nothing to
    // do (avoids reconnecting every time focus merely moves within the
    // same SSH pane's split panes, or the pane just gets re-focused).
    if (settings.has_value() == m_settings.has_value() && (!settings || (settings->host == m_settings->host && settings->port == m_settings->port
                                                                          && settings->username == m_settings->username)))
        return;

    teardownSession();
    m_settings = settings;
    m_model->setRoot(QString());
    updatePathLabel();
    m_upAction->setEnabled(false);
    m_dotDotButton->setVisible(false);

    if (!m_settings) {
        setStatus(QStringLiteral("Not an SSH connection"));
        m_view->setEnabled(false);
        m_toolbar->setEnabled(false);
        return;
    }

    m_view->setEnabled(true);
    m_toolbar->setEnabled(true);
    setStatus(QStringLiteral("Connecting..."));

    m_session = new SftpSession(*m_settings, this);
    m_view->setSession(m_session);

    connect(m_session, &SftpSession::connected, this, &SftpDock::onConnected);
    connect(m_session, &SftpSession::connectFailed, this, &SftpDock::onConnectFailed);
    connect(m_session, &SftpSession::directoryListed, this, &SftpDock::onDirectoryListed);
    connect(m_session, &SftpSession::operationFailed, this, &SftpDock::onOperationFailed);
    connect(m_session, &SftpSession::operationSucceeded, this, &SftpDock::onOperationSucceeded);
    connect(m_session, &SftpSession::transferStarted, this, &SftpDock::onTransferStarted);
    connect(m_session, &SftpSession::transferProgress, this, &SftpDock::onTransferProgress);
    connect(m_session, &SftpSession::transferFinished, this, &SftpDock::onTransferFinished);
    connect(m_view, &SftpBrowserView::uploadRequested, this, &SftpDock::onUploadRequested);

    m_session->connectToHost();
}

void SftpDock::onConnected(const QString &homePath)
{
    m_model->setRoot(homePath);
    setStatus(QStringLiteral("Connected: %1").arg(homePath));
    enterDirectory(QModelIndex()); // the model's invisible root *is* homePath
}

void SftpDock::onConnectFailed(const QString &message)
{
    setStatus(QStringLiteral("Connection failed: %1").arg(message));
}

void SftpDock::enterDirectory(const QModelIndex &dir)
{
    m_view->setRootIndex(dir);
    updatePathLabel();
    m_upAction->setEnabled(dir.isValid());
    m_dotDotButton->setVisible(dir.isValid());

    if (!m_model->isLoaded(dir)) {
        const QString path = m_model->pathForIndex(dir);
        m_model->markLoading(path);
        m_session->listDirectory(path);
    }
}

void SftpDock::updatePathLabel()
{
    const QString path = m_model->pathForIndex(m_view->rootIndex());
    m_pathLabel->setText(path.isEmpty() ? QStringLiteral("(not connected)") : path);
}

void SftpDock::goUp()
{
    const QModelIndex current = m_view->rootIndex();
    if (current.isValid())
        enterDirectory(m_model->parent(current));
}

void SftpDock::onItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    if (m_model->isDirectory(index))
        enterDirectory(index);
    else
        downloadSelected(); // Explorer-"open"-like convenience; double-click always sets currentIndex() first
}

void SftpDock::onDirectoryListed(const QString &path, const QVector<SftpEntry> &entries)
{
    m_model->applyDirectoryListing(path, entries);
}

void SftpDock::onOperationFailed(const QString &operation, const QString &path, const QString &message)
{
    m_model->markLoadFailed(path);
    setStatus(QStringLiteral("%1 failed for %2: %3").arg(operation, path, message));
}

void SftpDock::onOperationSucceeded(const QString &operation, const QString &path)
{
    setStatus(QStringLiteral("%1 OK: %2").arg(operation, path));

    if (operation == QStringLiteral("delete") || operation == QStringLiteral("rename")) {
        m_model->removeEntry(path);
    } else if (operation == QStringLiteral("mkdir") || operation == QStringLiteral("upload")) {
        // Re-list the parent so the new entry (with a real size, for an
        // upload) shows up, rather than trying to patch the tree by hand.
        const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
        const QString parentPath = lastSlash > 0 ? path.left(lastSlash) : QStringLiteral("/");
        m_model->markLoading(parentPath);
        m_session->listDirectory(parentPath);
    }
}

void SftpDock::onTransferStarted(const QString &path, qint64 totalBytes)
{
    Q_UNUSED(totalBytes);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    setStatus(QStringLiteral("Transferring %1...").arg(path));
}

void SftpDock::onTransferProgress(const QString &path, qint64 bytesDone, qint64 totalBytes)
{
    Q_UNUSED(path);
    if (totalBytes > 0)
        m_progressBar->setValue(static_cast<int>(bytesDone * 100 / totalBytes));
}

void SftpDock::onTransferFinished(const QString &path, bool ok)
{
    m_progressBar->setVisible(false);
    setStatus(ok ? QStringLiteral("Done: %1").arg(path) : QStringLiteral("Failed: %1").arg(path));
}

void SftpDock::onUploadRequested(const QString &localPath, const QString &remotePath)
{
    if (!m_session)
        return;

    if (QFileInfo(localPath).isDir())
        m_session->uploadDirectory(localPath, remotePath);
    else
        m_session->uploadFile(localPath, remotePath);
}

void SftpDock::uploadFiles()
{
    if (!m_session)
        return;

    const QStringList localPaths = QFileDialog::getOpenFileNames(this, QStringLiteral("Upload Files"));
    const QString targetDir = m_model->pathForIndex(m_view->rootIndex());

    for (const QString &localPath : localPaths) {
        const QFileInfo info(localPath);
        m_session->uploadFile(localPath, joinRemotePath(targetDir, info.fileName()));
    }
}

void SftpDock::showItemContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    if (!index.isValid() || !m_session)
        return;

    // Right-clicking a row that wasn't already selected should act on
    // that row, not whatever was selected before - same reasoning as the
    // tab bar's own right-click context menu (showTabContextMenu()).
    m_view->setCurrentIndex(index);

    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("Download...")), &QAction::triggered, this, &SftpDock::downloadSelected);
    connect(menu.addAction(QStringLiteral("Rename...")), &QAction::triggered, this, &SftpDock::renameSelected);
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Delete")), &QAction::triggered, this, &SftpDock::deleteSelected);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void SftpDock::downloadSelected()
{
    if (!m_session)
        return;

    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid()) {
        QMessageBox::information(this, QStringLiteral("Download"), QStringLiteral("Select a file or folder to download."));
        return;
    }

    if (m_model->isDirectory(current)) {
        const QString destParent = QFileDialog::getExistingDirectory(this, QStringLiteral("Download Folder To"));
        if (destParent.isEmpty())
            return;
        const QString localPath = destParent + QLatin1Char('/') + m_model->nameOf(current);
        m_session->downloadDirectory(m_model->pathForIndex(current), localPath);
        return;
    }

    const QString remotePath = m_model->pathForIndex(current);
    const QString localPath = QFileDialog::getSaveFileName(this, QStringLiteral("Download File"), m_model->nameOf(current));
    if (!localPath.isEmpty())
        m_session->downloadFile(remotePath, localPath);
}

void SftpDock::uploadFolder()
{
    if (!m_session)
        return;

    const QString localPath = QFileDialog::getExistingDirectory(this, QStringLiteral("Upload Folder"));
    if (localPath.isEmpty())
        return;

    const QFileInfo info(localPath);
    const QString targetDir = m_model->pathForIndex(m_view->rootIndex());
    m_session->uploadDirectory(localPath, joinRemotePath(targetDir, info.fileName()));
}

void SftpDock::newFolder()
{
    if (!m_session)
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty())
        return;

    const QString parentDir = m_model->pathForIndex(m_view->rootIndex());
    m_model->addPlaceholderDirectory(parentDir, name);
    m_session->makeDirectory(joinRemotePath(parentDir, name));
}

void SftpDock::deleteSelected()
{
    if (!m_session)
        return;

    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid())
        return;

    const QString path = m_model->pathForIndex(current);
    const auto reply = QMessageBox::question(this, QStringLiteral("Delete"), QStringLiteral("Delete \"%1\"?").arg(m_model->nameOf(current)));
    if (reply != QMessageBox::Yes)
        return;

    if (m_model->isDirectory(current))
        m_session->removeDirectory(path); // recursive if not empty, see SftpClient::removeDirectory
    else
        m_session->removeFile(path);
}

void SftpDock::renameSelected()
{
    if (!m_session)
        return;

    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid())
        return;

    const QString oldPath = m_model->pathForIndex(current);
    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"), QLineEdit::Normal, m_model->nameOf(current), &ok);
    if (!ok || newName.isEmpty() || newName == m_model->nameOf(current))
        return;

    const QString newPath = joinRemotePath(m_model->pathForIndex(current.parent()), newName);
    m_session->renameEntry(oldPath, newPath);
}

void SftpDock::refresh()
{
    if (!m_session)
        return;

    const QModelIndex dir = m_view->rootIndex();
    const QString path = m_model->pathForIndex(dir);

    m_model->markLoading(path);
    m_session->listDirectory(path);
}

void SftpDock::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

#include "SftpDock.moc"
