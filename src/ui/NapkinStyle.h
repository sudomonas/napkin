#pragma once
#include <QProxyStyle>

namespace napkin {

// Napkin's own look for the standard controls, the same on every platform
// (SPEC.md §7, "The design system"). Drawn over Fusion, which is Qt's own and
// identical everywhere, and which draws from the palette — so everything here
// follows the theme without a stylesheet.
//
// What it draws itself: buttons (plain, flat, default, round, pill and the
// segments of a segmented control), text fields, combo boxes, spin boxes,
// check boxes, scroll bars, menus, tooltips, group boxes and the splitter.
// Anything else is Fusion's.
//
// A widget asks for a shape with the "napkinShape" property:
//   "circle"  — a round icon button (the header's New, Settings, Menu)
//   "pill"    — fully rounded ends (the search field, a text button in the header)
//   "segment" — one option of a segmented control; checked is the chosen one
class NapkinStyle : public QProxyStyle {
    Q_OBJECT
public:
    NapkinStyle();

    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;
    using QProxyStyle::polish;
    using QProxyStyle::unpolish;

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                           const QSize& size, const QWidget* widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                         SubControl sub, const QWidget* widget) const override;
    QRect subElementRect(SubElement element, const QStyleOption* option,
                         const QWidget* widget) const override;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override;

    static constexpr const char* kShapeProperty = "napkinShape";
    // Set on a container that paints itself as a surface (the sidebar), so the
    // controls in it are drawn cut out of it rather than as more surface.
    static constexpr const char* kSurfaceProperty = "napkinSurface";
};

}  // namespace napkin
