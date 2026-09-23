#pragma once

#include <QWidget>

#include "core/ConnectionProfile.h"

class ProfileStore;

// Shown as a new pane's content the instant Split Right/Down fires, before
// any connection is chosen - replaces the old modal chooser (QMessageBox ->
// type menu -> settings dialog) with a single non-modal in-pane picker.
// This class knows nothing about Transport/how a connection actually gets
// made - it only reports what the user picked. MainWindow (which already
// owns that construction logic) turns a signal into a real pane - see
// MainWindow::splitActivePane()/finishSplitWithResult().
class SplitPicker : public QWidget
{
    Q_OBJECT

public:
    explicit SplitPicker(ProfileStore *store, QWidget *parent = nullptr);

signals:
    void duplicateRequested();
    void profileChosen(const ConnectionProfile &profile);
    void newSerialRequested();
    void newTelnetRequested();
    void newSshRequested();
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent *event) override;
};
