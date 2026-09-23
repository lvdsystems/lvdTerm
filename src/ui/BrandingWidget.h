#pragma once

#include <QWidget>

// Shared "LVD Systems S.r.l. / lvdterm / version" text block used by both
// WelcomeWidget (the startup tab) and AboutDialog, so the two never drift.
// Text-only for now - no logo file yet; laid out so an image label could be
// inserted above the text later without restructuring.
class BrandingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BrandingWidget(QWidget *parent = nullptr);
};
