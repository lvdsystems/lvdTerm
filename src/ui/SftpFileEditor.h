#pragma once

#include <QTemporaryDir>
#include <QWidget>

#include "transports/ssh/SshConnectionSettings.h"

class QCloseEvent;
class QCheckBox;
class QLabel;
class QLineEdit;
class QShortcut;
class QTimer;
class QPushButton;
class CodeEditor;
class SftpSession;

// A small text editor for one remote file, opened straight from the SFTP
// browser. Entirely self-contained so it keeps working however the dock
// changes afterwards (another pane focused = the dock moves to another
// host): it opens its own SFTP connection, downloads the file into a
// private temporary folder, edits it there, uploads on Save (Ctrl+S), and
// removes the temporary folder when it closes. Closing with unsaved
// changes asks first.
//
// Text only: binary files, and anything that isn't valid UTF-8 (editing
// those as text and saving would silently corrupt them), are refused with
// an explanation. CRLF line endings are preserved.
class SftpFileEditor : public QWidget
{
    Q_OBJECT

public:
    // Files over this aren't opened in the editor (it's for config files
    // and scripts, not data) - the caller checks before connecting.
    static constexpr qint64 kMaxBytes = 5 * 1024 * 1024;

    SftpFileEditor(const SshConnectionSettings &settings, const QString &remotePath, QWidget *parent = nullptr);
    ~SftpFileEditor() override;

    const QString &remotePath() const { return m_remotePath; }

    // False if the user cancelled closing (or saving failed). Asks about
    // unsaved changes - also used by MainWindow when the whole app quits.
    bool requestClose();

signals:
    void saved(const QString &remotePath); // an upload finished OK
    void saveFinished(bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void onConnected();
    void onOperationSucceeded(const QString &operation, const QString &path);
    void onOperationFailed(const QString &operation, const QString &path, const QString &message);
    void loadDownloadedFile();
    bool startSave();
    bool saveAndWait();
    void showFindBar();
    void hideFindBar();
    void findStep(bool backwards);
    void refreshFindHighlights(bool selectMatch);
    void setStatus(const QString &text);
    void updateTitle();

    SshConnectionSettings m_settings;
    QString m_remotePath;
    QTemporaryDir m_tempDir; // holds the working copy; deleted with this object
    QString m_localPath;

    SftpSession *m_session = nullptr;
    CodeEditor *m_edit = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLabel *m_status = nullptr;

    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QCheckBox *m_findCase = nullptr;
    QLabel *m_findCount = nullptr;
    QShortcut *m_escapeShortcut = nullptr;
    QTimer *m_findRefresh = nullptr; // coalesces re-highlighting while typing in the document
    QString m_language;

    bool m_loaded = false;
    bool m_saving = false;
    bool m_crlf = false; // the original used \r\n - restored on save
    bool m_lastSaveOk = false;
};
