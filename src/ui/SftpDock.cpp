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
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
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
// Lays children out left to right and wraps onto further rows when the
// width runs out (Qt's own "Flow Layout" example, trimmed). A QToolBar
// instead hides whatever doesn't fit behind an overflow chevron - which,
// in a narrow dock, left only the first button or two visible.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent, int spacing = 2) : QLayout(parent), m_spacing(spacing) {}
    ~FlowLayout() override { qDeleteAll(m_items); }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return static_cast<int>(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index, nullptr); }
    QLayoutItem *takeAt(int index) override { return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr; }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        const QMargins m = contentsMargins();
        const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = area.x();
        int y = area.y();
        int rowHeight = 0;

        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            if (x > area.x() && x + hint.width() > area.right() + 1) { // wrap
                x = area.x();
                y += rowHeight + m_spacing;
                rowHeight = 0;
            }
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + m_spacing;
            rowHeight = qMax(rowHeight, hint.height());
        }
        return y + rowHeight - rect.y() + m.bottom();
    }

    QList<QLayoutItem *> m_items;
    int m_spacing;
};

// Clickable breadcrumb for the current remote path: "/ > home > lvd >
// Pictures", every folder except the current one a flat button that
// highlights on hover (click = go there), plus a double-click anywhere on
// the bar to copy the full path. Knows nothing about SFTP - it only
// reports which path was clicked/copied.
class PathBar : public QScrollArea
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr) : QScrollArea(parent), m_content(new QWidget)
    {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFixedHeight(26);
        setWidget(m_content);

        m_layout = new QHBoxLayout(m_content);
        m_layout->setContentsMargins(4, 0, 4, 0);
        m_layout->setSpacing(0);

        // palette()-based so it follows the light/dark theme.
        m_content->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; border-radius: 3px; padding: 2px 5px; }"
            "QToolButton:hover:enabled { background-color: palette(highlight); color: palette(highlighted-text); }"
            "QToolButton:disabled { color: palette(text); font-weight: bold; }"
            "QLabel { color: palette(mid); }"));
    }

    void setPath(const QString &path)
    {
        m_path = path;

        // Rebuilt from scratch on every change - a path has a handful of
        // segments, not worth diffing. deleteLater(): this can run from
        // inside one of the old buttons' own clicked() signal.
        while (QLayoutItem *item = m_layout->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete item;
        }

        if (path.isEmpty()) {
            m_layout->addWidget(new QLabel(QStringLiteral("(not connected)"), m_content));
            m_layout->addStretch(1);
            return;
        }

        QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString accumulated;
        addSegment(QStringLiteral("/"), QStringLiteral("/"), parts.isEmpty());
        for (int i = 0; i < parts.size(); ++i) {
            accumulated += QLatin1Char('/') + parts.at(i);
            m_layout->addWidget(new QLabel(QStringLiteral("\u203A"), m_content)); // single right-pointing angle quote
            addSegment(parts.at(i), accumulated, i == parts.size() - 1);
        }
        m_layout->addStretch(1);

        // Long paths: keep the tail (where the current folder is) in view.
        QTimer::singleShot(0, this, [this] { horizontalScrollBar()->setValue(horizontalScrollBar()->maximum()); });
    }

signals:
    void segmentClicked(const QString &path);
    void copyRequested(const QString &path);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (!m_path.isEmpty())
            emit copyRequested(m_path);
        event->accept();
    }

    // Buttons swallow the double-click (they treat it as another press),
    // so catch it here. After the first click of a double-click on a
    // folder, the bar has already been rebuilt to end at that folder -
    // so what gets copied is the folder that was double-clicked.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonDblClick && !m_path.isEmpty()) {
            emit copyRequested(m_path);
            return true;
        }
        return QScrollArea::eventFilter(watched, event);
    }

private:
    void addSegment(const QString &label, const QString &fullPath, bool isCurrent)
    {
        auto *button = new QToolButton(m_content);
        button->setText(label);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->installEventFilter(this);
        if (isCurrent) {
            button->setEnabled(false); // styled as the bold "you are here" segment
            button->setToolTip(QStringLiteral("%1\nDouble-click to copy the path").arg(fullPath));
        } else {
            button->setCursor(Qt::PointingHandCursor);
            button->setToolTip(QStringLiteral("Go to %1\nDouble-click to copy the path").arg(fullPath));
            connect(button, &QToolButton::clicked, this, [this, fullPath] { emit segmentClicked(fullPath); });
        }
        m_layout->addWidget(button);
    }

    QWidget *m_content = nullptr;
    QHBoxLayout *m_layout = nullptr;
    QString m_path;
};

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
    // Browser-style history navigation: a mouse's back/forward side
    // buttons, the Back/Forward media keys (Qt::Key_Back/Key_Forward,
    // what Logitech-style button remapping sends), or Alt+Left/Right.
    void backRequested();
    void forwardRequested();
    void deleteRequested(); // Delete key - same as the toolbar's Delete (which asks first)

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        const bool alt = event->modifiers() == Qt::AltModifier;
        if (event->key() == Qt::Key_Back || (alt && event->key() == Qt::Key_Left)) {
            emit backRequested();
            event->accept();
        } else if (event->key() == Qt::Key_Forward || (alt && event->key() == Qt::Key_Right)) {
            emit forwardRequested();
            event->accept();
        } else if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier) {
            emit deleteRequested();
            event->accept();
        } else {
            QTableView::keyPressEvent(event);
        }
    }

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
        if (event->button() == Qt::BackButton) {
            emit backRequested();
            event->accept();
            return;
        }
        if (event->button() == Qt::ForwardButton) {
            emit forwardRequested();
            event->accept();
            return;
        }
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
    , m_pathBar(new PathBar(this))
    , m_statusLabel(new QLabel(QStringLiteral("Not connected"), this))
    , m_progressBar(new QProgressBar(this))
{
    setObjectName(QStringLiteral("SftpDock"));

    m_view->setModel(m_model);
    m_view->setSftpModel(m_model);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTableView::doubleClicked, this, &SftpDock::onItemDoubleClicked);
    connect(m_view, &QTableView::customContextMenuRequested, this, &SftpDock::showItemContextMenu);
    connect(m_view, &SftpBrowserView::backRequested, this, &SftpDock::goBack);
    connect(m_view, &SftpBrowserView::forwardRequested, this, &SftpDock::goForward);
    connect(m_view, &SftpBrowserView::deleteRequested, this, &SftpDock::deleteSelected);
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

    connect(m_pathBar, &PathBar::segmentClicked, this, &SftpDock::navigateToPath);
    connect(m_pathBar, &PathBar::copyRequested, this, &SftpDock::copyPath);

    // Labelled buttons (the standard icons alone don't say which of
    // "upload"/"download"/"new folder" is which; the full "..." text stays
    // as the tooltip) in a wrapping flow, so a narrow dock gets more rows
    // rather than losing buttons.
    m_toolbar = new QWidget(this);
    auto *flow = new FlowLayout(m_toolbar);
    flow->setContentsMargins(4, 2, 4, 2);

    auto addToolAction = [&](const QIcon &icon, const QString &label, const QString &tooltip, auto slot) {
        auto *action = new QAction(icon, label, this);
        action->setToolTip(tooltip);
        connect(action, &QAction::triggered, this, slot);

        auto *button = new QToolButton(m_toolbar);
        button->setDefaultAction(action); // enabled state and tooltip follow the action
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        flow->addWidget(button);
        return action;
    };
    auto stdIcon = [this](QStyle::StandardPixmap pixmap) { return style()->standardIcon(pixmap); };

    m_upAction = addToolAction(makeUpLevelIcon(), QStringLiteral("Up"), QStringLiteral("Up"), &SftpDock::goUp);
    m_upAction->setEnabled(false);
    addToolAction(stdIcon(QStyle::SP_ArrowUp), QStringLiteral("Upload"), QStringLiteral("Upload..."), &SftpDock::uploadFiles);
    addToolAction(stdIcon(QStyle::SP_DirOpenIcon), QStringLiteral("Upload Folder"), QStringLiteral("Upload Folder..."), &SftpDock::uploadFolder);
    addToolAction(stdIcon(QStyle::SP_ArrowDown), QStringLiteral("Download"), QStringLiteral("Download..."), &SftpDock::downloadSelected);
    addToolAction(stdIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("New Folder"), QStringLiteral("New Folder..."), &SftpDock::newFolder);
    // No good standard icon for rename - the text label alone, no icon.
    addToolAction(QIcon(), QStringLiteral("Rename"), QStringLiteral("Rename..."), &SftpDock::renameSelected);
    addToolAction(stdIcon(QStyle::SP_TrashIcon), QStringLiteral("Delete"), QStringLiteral("Delete"), &SftpDock::deleteSelected);
    addToolAction(stdIcon(QStyle::SP_BrowserReload), QStringLiteral("Refresh"), QStringLiteral("Refresh"), &SftpDock::refresh);

    auto *pathBar = new QWidget(this);
    auto *pathLayout = new QHBoxLayout(pathBar);
    pathLayout->setContentsMargins(2, 0, 2, 0);
    pathLayout->addWidget(m_pathBar, 1);

    // Same as double-clicking the bar, but findable without knowing that.
    m_copyPathButton = new QToolButton(this);
    m_copyPathButton->setAutoRaise(true);
    m_copyPathButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_copyPathButton->setText(QStringLiteral("Copy"));
    m_copyPathButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy"), style()->standardIcon(QStyle::SP_FileIcon)));
    m_copyPathButton->setToolTip(QStringLiteral("Copy path"));
    m_copyPathButton->setEnabled(false);
    connect(m_copyPathButton, &QToolButton::clicked, this, [this] { copyPath(m_model->pathForIndex(m_view->rootIndex())); });
    pathLayout->addWidget(m_copyPathButton);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toolbar);
    layout->addWidget(pathBar);
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

    // Indices into the old connection's model mean nothing to the next one.
    m_backStack.clear();
    m_forwardStack.clear();
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
    // Every navigation except back/forward themselves records where it
    // came from - and a fresh navigation discards the forward history,
    // like a browser.
    if (!m_navigatingHistory && QPersistentModelIndex(dir) != QPersistentModelIndex(m_view->rootIndex())) {
        m_backStack.append(QPersistentModelIndex(m_view->rootIndex()));
        m_forwardStack.clear();
    }

    m_view->setRootIndex(dir);
    updatePathLabel();
    m_upAction->setEnabled(dir.isValid());

    if (!m_model->isLoaded(dir)) {
        const QString path = m_model->pathForIndex(dir);
        m_model->markLoading(path);
        m_session->listDirectory(path);
    }
}

void SftpDock::updatePathLabel()
{
    const QString path = m_model->pathForIndex(m_view->rootIndex());
    m_pathBar->setPath(path);
    m_copyPathButton->setEnabled(!path.isEmpty());
}

void SftpDock::keyPressEvent(QKeyEvent *event)
{
    // The browser view handles these itself; this covers focus being on
    // something else in the dock (toolbar button, path label).
    if (event->key() == Qt::Key_Back) {
        goBack();
        event->accept();
    } else if (event->key() == Qt::Key_Forward) {
        goForward();
        event->accept();
    } else {
        QDockWidget::keyPressEvent(event);
    }
}

void SftpDock::goBack()
{
    if (!m_session || m_backStack.isEmpty())
        return;

    m_forwardStack.append(QPersistentModelIndex(m_view->rootIndex()));
    const QModelIndex target(m_backStack.takeLast());

    m_navigatingHistory = true;
    enterDirectory(target);
    m_navigatingHistory = false;
}

void SftpDock::goForward()
{
    if (!m_session || m_forwardStack.isEmpty())
        return;

    m_backStack.append(QPersistentModelIndex(m_view->rootIndex()));
    const QModelIndex target(m_forwardStack.takeLast());

    m_navigatingHistory = true;
    enterDirectory(target);
    m_navigatingHistory = false;
}

void SftpDock::copyPath(const QString &path)
{
    if (path.isEmpty())
        return;

    QGuiApplication::clipboard()->setText(path);
    setStatus(QStringLiteral("Copied: %1").arg(path));
}

void SftpDock::navigateToPath(const QString &path)
{
    if (!m_session)
        return;

    if (path == m_model->rootPath()) {
        enterDirectory(QModelIndex());
        return;
    }

    const QModelIndex index = m_model->indexForPath(path);
    if (index.isValid()) {
        enterDirectory(index);
        return;
    }

    // Above the folder this connection started in (e.g. "/home" while
    // the model is rooted at "/home/lvd"): the model only holds the
    // subtree under its root, so re-root it there - the same thing
    // onConnected() does for the home directory. Its indices are gone
    // after that, so the back/forward history goes too.
    m_backStack.clear();
    m_forwardStack.clear();
    m_model->setRoot(path);
    enterDirectory(QModelIndex());
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
