#pragma once

#include <QDialog>

// Help > About lvdterm... - branding, a short description, and buttons to
// view the full license / third-party notices (embedded as Qt resources
// from the repo-root LICENSE / THIRD_PARTY_LICENSES.md - see
// src/resources/license.qrc - so they're always available regardless of
// how the app was deployed).
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    void showResourceText(const QString &title, const QString &resourcePath);
};
