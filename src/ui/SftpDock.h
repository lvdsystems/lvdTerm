#pragma once

#include <optional>

#include <QDockWidget>

#include "sftp/SftpModel.h"
#include "transports/ssh/SshConnectionSettings.h"

class QAction;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolBar;
class SftpSession;
class SftpBrowserView;

// Dockable remote file browser for the currently active SSH pane (see
// MainWindow::updateSftpDockForActiveView()) - a second SFTP session to
// the same host (see SftpClient), lazily listing directories as they're
// entered. Explorer-style single-folder navigation (double-click a
// folder to enter it, an Up button to go back out) rather than a tree -
// see enterDirectory()/goUp(). Supports drag-in from Explorer (upload,
// files or whole folders - recursive) and drag-out to Explorer for a
// single small file (SftpBrowserView::startDrag() - materialized to a
// temp file synchronously before the OS drag starts, since Qt's drag API
// has no cross-platform way to hand Explorer bytes lazily on drop).
// Anything bigger - a folder, a large file, or a multi-selection - isn't
// dragged out at all; use "Download..." instead, which is fully
// non-blocking and streams straight to a chosen destination with no temp
// file. Toolbar covers up/upload/upload-folder/download/new folder/
// delete/rename/refresh; delete recurses into non-empty directories (see
// SftpClient::removeDirectory).
//
// Not implemented: remote-to-remote copy (SFTP has no native
// server-side copy; only cut+paste/rename, which is a same-filesystem
// move, is offered), and a persistent transfer history (only the
// current transfer's progress is shown).
class SftpDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit SftpDock(QWidget *parent = nullptr);
    ~SftpDock() override;

    // Called by MainWindow when the active pane changes. std::nullopt
    // means the active pane isn't SSH-backed (or there is none) - the
    // whole dock (browser + toolbar) is grayed out in that case, since
    // there's nothing it could act on.
    void setActiveConnection(const std::optional<SshConnectionSettings> &settings);

private slots:
    void onConnected(const QString &homePath);
    void onConnectFailed(const QString &message);
    void onDirectoryListed(const QString &path, const QVector<SftpEntry> &entries);
    void onOperationFailed(const QString &operation, const QString &path, const QString &message);
    void onOperationSucceeded(const QString &operation, const QString &path);
    void onTransferStarted(const QString &path, qint64 totalBytes);
    void onTransferProgress(const QString &path, qint64 bytesDone, qint64 totalBytes);
    void onTransferFinished(const QString &path, bool ok);
    void onUploadRequested(const QString &localPath, const QString &remotePath);
    void onItemDoubleClicked(const QModelIndex &index); // enters a folder, or downloads a file
    void showItemContextMenu(const QPoint &pos); // right-click on a file/folder: Download/Rename/Delete

    void goUp();
    void uploadFiles();
    void uploadFolder();
    void downloadSelected();
    void newFolder();
    void deleteSelected();
    void renameSelected();
    void refresh();

private:
    // Navigates the browser to show `dir`'s contents (dir must be a
    // directory index, or invalid for the connection's home directory),
    // requesting a listing first if it hasn't been loaded yet. Every
    // navigation - connecting, double-clicking a folder, Up - goes
    // through this one place.
    void enterDirectory(const QModelIndex &dir);
    void updatePathLabel();
    void setStatus(const QString &text);
    void teardownSession();

    SftpModel *m_model = nullptr;
    SftpBrowserView *m_view = nullptr;
    QToolBar *m_toolbar = nullptr;
    QAction *m_upAction = nullptr;
    QPushButton *m_dotDotButton = nullptr; // ".." row above the listing - same target as m_upAction, see the constructor
    QLabel *m_pathLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;

    SftpSession *m_session = nullptr;
    std::optional<SshConnectionSettings> m_settings;
};
