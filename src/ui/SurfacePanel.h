#pragma once
#include "NapkinStyle.h"
#include "Tokens.h"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <functional>

namespace napkin {

// A surface on the window: rounded like a card, filled like one, no edge. The
// main window's sidebar and the Settings dialog's page list are both one, so
// they are the same thing rather than two that look alike. Marked as a surface
// for NapkinStyle, so the controls inside it are drawn cut out of it.
class SurfacePanel : public QWidget {
public:
    explicit SurfacePanel(QWidget* parent = nullptr) : QWidget(parent)
    {
        setProperty(NapkinStyle::kSurfaceProperty, true);
    }

    std::function<void(int)> widthChanged;   // the main window's search field follows it

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), tokens::kCardRadius, tokens::kCardRadius);
        p.fillPath(path, palette().color(QPalette::Base));
    }
    // Only while it is on screen: laid out inside a hidden page (the start
    // page showing instead) it was given 320px, and the search field jumped
    // from 320 to 344 when the first napkin appeared.
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        if (widthChanged && isVisible()) widthChanged(width());
    }
    void showEvent(QShowEvent* e) override
    {
        QWidget::showEvent(e);
        if (widthChanged) widthChanged(width());
    }
};

}  // namespace napkin
