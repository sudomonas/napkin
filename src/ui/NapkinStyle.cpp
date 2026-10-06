#include "NapkinStyle.h"
#include "Tokens.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOption>
#include <QToolButton>
#include <cmath>

namespace napkin {

using namespace tokens;

namespace {

QString shapeOf(const QWidget* w)
{
    return w ? w->property(NapkinStyle::kShapeProperty).toString() : QString();
}

// What a control does under the pointer and under a press: a step of the text
// colour over whatever it sits on, so it reads on both themes and on any fill.
QColor hoverTint(const QPalette& pal)   { return text(pal, kControlHoverAlpha); }
QColor pressTint(const QPalette& pal)   { return text(pal, kControlPressAlpha); }

QPainterPath rounded(const QRectF& r, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    return path;
}

qreal radiusFor(const QString& shape, const QRectF& r)
{
    if (shape == QLatin1String("circle") || shape == QLatin1String("pill")
        || shape == QLatin1String("segment"))
        return std::min(r.width(), r.height()) / 2.0;
    return kControlRadius;
}

// The keyboard's place: a solid ring in the readable accent, just outside the
// control's own edge. Never colour alone — it is a shape that appears.
void drawFocusRing(QPainter* p, const QRectF& r, qreal radius, const QPalette& pal)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(readableAccent(pal, 1.0), kFocusWidth));
    const qreal in = kFocusWidth / 2.0;
    p->drawPath(rounded(r.adjusted(in, in, -in, -in), std::max<qreal>(0.0, radius - in)));
    p->restore();
}

void drawChevron(QPainter* p, const QRectF& box, bool down, const QColor& colour)
{
    const qreal s = std::min(box.width(), box.height()) * 0.5;
    const QPointF c = box.center();
    QPainterPath path;
    if (down) {
        path.moveTo(c.x() - s * 0.5, c.y() - s * 0.22);
        path.lineTo(c.x(), c.y() + s * 0.28);
        path.lineTo(c.x() + s * 0.5, c.y() - s * 0.22);
    } else {
        path.moveTo(c.x() - s * 0.5, c.y() + s * 0.22);
        path.lineTo(c.x(), c.y() - s * 0.28);
        path.lineTo(c.x() + s * 0.5, c.y() + s * 0.22);
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);
    p->restore();
}

bool isOn(const QStyleOption* o, QStyle::StateFlag f) { return o->state & f; }

// A control is cut out of whatever it sits on: on the window it is a lighter
// surface (the header's search field, buttons); on a surface — a settings
// group, the sidebar — it takes the window's colour, as the chosen segment of
// the Home / Trash switch does. One fill for both made every field in Settings
// vanish into the group it sat in.
bool onSurface(const QWidget* w)
{
    for (const QWidget* p = w ? w->parentWidget() : nullptr; p; p = p->parentWidget()) {
        if (p->property(NapkinStyle::kSurfaceProperty).toBool()) return true;
        if (p->inherits("QGroupBox")) return true;
        if (p->isWindow()) break;
    }
    return false;
}

QColor fieldFill(const QPalette& pal, const QWidget* w)
{
    return pal.color(onSurface(w) ? QPalette::Window : QPalette::Base);
}

QColor controlFill(const QPalette& pal, const QWidget* w)
{
    return pal.color(onSurface(w) ? QPalette::Window : QPalette::Button);
}

}  // namespace

NapkinStyle::NapkinStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("fusion")))
{
    setObjectName(QStringLiteral("napkin"));
}

void NapkinStyle::polish(QWidget* w)
{
    QProxyStyle::polish(w);
    if (qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w)
        || qobject_cast<QLineEdit*>(w))
        w->setAttribute(Qt::WA_Hover, true);
    // Rounded corners need the window behind them to be see-through. Set while
    // polishing, before the popup's native window exists.
    if (qobject_cast<QMenu*>(w)) w->setAttribute(Qt::WA_TranslucentBackground, true);
    // A combo box's list is a popup like a menu and should look like one: it
    // was a square opaque window in the desk colour with a rounded rim.
    if (w->inherits("QComboBoxPrivateContainer")) {
        w->setAttribute(Qt::WA_TranslucentBackground, true);
        QPalette pal = w->palette();
        pal.setColor(QPalette::Window, pal.color(QPalette::Base));
        w->setPalette(pal);
        // The list inside is already its child by now; when it was polished it
        // was not yet, so it is made see-through from here.
        for (auto* view : w->findChildren<QAbstractItemView*>()) {
            view->setAutoFillBackground(false);
            view->viewport()->setAutoFillBackground(false);
            QPalette vp = view->palette();
            vp.setColor(QPalette::Base, Qt::transparent);
            vp.setColor(QPalette::Window, Qt::transparent);
            view->setPalette(vp);
        }
    }
    if (auto* view = qobject_cast<QAbstractItemView*>(w);
        view && view->parentWidget() && view->parentWidget()->inherits("QComboBoxPrivateContainer")) {
        view->setAutoFillBackground(false);
        view->viewport()->setAutoFillBackground(false);
    }
}

void NapkinStyle::unpolish(QWidget* w)
{
    if (qobject_cast<QMenu*>(w) || w->inherits("QComboBoxPrivateContainer"))
        w->setAttribute(Qt::WA_TranslucentBackground, false);
    QProxyStyle::unpolish(w);
}

int NapkinStyle::pixelMetric(PixelMetric m, const QStyleOption* o, const QWidget* w) const
{
    switch (m) {
    case PM_ScrollBarExtent:      return kScrollBarExtent;
    case PM_ScrollBarSliderMin:   return 36;
    case PM_SplitterWidth:        return kSplitterGap;
    case PM_MenuPanelWidth:       return 1;
    case PM_MenuHMargin:          return 6;
    case PM_MenuVMargin:          return 6;
    case PM_SubMenuOverlap:       return -4;
    case PM_ToolTipLabelFrameWidth: return 8;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:      // grows with the text it labels, from 18px
        return std::max(18, w ? QFontMetrics(w->font()).height() : 18);
    case PM_MenuButtonIndicator:
        // The header's menu button opens a menu and says so with its glyph;
        // a second, drawn arrow beside it was two symbols for one idea.
        if (qobject_cast<const QToolButton*>(w)) return 0;
        break;
    case PM_SmallIconSize:
        // A field's glyph (the search magnifier) grows with the field's text.
        if (qobject_cast<const QLineEdit*>(w))
            return std::max(16, QFontMetrics(w->font()).height() * 9 / 10);
        break;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:  return 0;
    case PM_DefaultFrameWidth:
        if (qobject_cast<const QLineEdit*>(w)) return 1;
        break;
    default: break;
    }
    return QProxyStyle::pixelMetric(m, o, w);
}

int NapkinStyle::styleHint(StyleHint h, const QStyleOption* o, const QWidget* w,
                           QStyleHintReturn* r) const
{
    switch (h) {
    // A letter underlined in every label is a 1990s menu bar's affordance; the
    // shortcuts are listed in the menu itself.
    case SH_UnderlineShortcut:                 return 0;
    case SH_DialogButtonBox_ButtonsHaveIcons:  return 0;
    case SH_Menu_Scrollable:                   return 1;
    case SH_ScrollBar_ContextMenu:             return 0;
    case SH_ScrollBar_LeftClickAbsolutePosition: return 1;
    case SH_ToolTipLabel_Opacity:              return 255;
    default: break;
    }
    return QProxyStyle::styleHint(h, o, w, r);
}

QSize NapkinStyle::sizeFromContents(ContentsType type, const QStyleOption* o,
                                    const QSize& size, const QWidget* w) const
{
    QSize s = QProxyStyle::sizeFromContents(type, o, size, w);
    switch (type) {
    case CT_PushButton:
        // Room either side of the words, measured from the words themselves:
        // Fusion's own margin left "Empty trash…" 7px from its rounded ends.
        if (shapeOf(w).isEmpty()) {
            s.setWidth(std::max(s.width(), size.width() + 2 * kControlPadX));
            s.setHeight(std::max(s.height(), kFieldHeight));
        } else if (shapeOf(w) == QLatin1String("pill")) {
            s.setWidth(std::max(s.width(), size.width() + 2 * kPillPadX));
            s.setHeight(std::max(s.height(), kFieldHeight));
        }
        break;
    case CT_LineEdit:
    case CT_ComboBox:
    case CT_SpinBox:
        s.setHeight(std::max(s.height(), kFieldHeight));
        if (type == CT_ComboBox) s.setWidth(s.width() + 8);
        break;
    case CT_MenuItem:
        if (const auto* mi = qstyleoption_cast<const QStyleOptionMenuItem*>(o)) {
            if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
                if (mi->text.isEmpty()) s.setHeight(9);
                else                    s.setHeight(s.height() + 6);   // a section title
            } else {
                s.setHeight(std::max(s.height() + 4, 26));
                s.setWidth(s.width() + 16);
            }
        }
        break;
    default: break;
    }
    return s;
}

QRect NapkinStyle::subElementRect(SubElement se, const QStyleOption* o, const QWidget* w) const
{
    QRect r = QProxyStyle::subElementRect(se, o, w);
    if (se == SE_ItemViewItemText && shapeOf(w) == QLatin1String("nav")) {
        // The page's name as far in as a field's text, not against the edge.
        return visualRect(o->direction, o->rect,
                          visualRect(o->direction, o->rect, r).adjusted(kFieldInset - 4, 0, 0, 0));
    }
    if (se == SE_LineEditContents) {
        // Text starts where a combo's does, clear of the rounded end; it sat
        // 3px from the edge, and the rename field's text touched it.
        const auto* le = qstyleoption_cast<const QStyleOptionFrame*>(o);
        if (le && le->lineWidth > 0) r.adjust(kFieldInset - 3, 0, -(kFieldInset - 3), 0);
    }
    return r;
}

QRect NapkinStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex* o,
                                  SubControl sc, const QWidget* w) const
{
    if (cc == CC_ScrollBar) {
        // No arrow buttons: the groove is the whole bar and the slider is sized
        // and placed in it directly. Wheel, drag and page-click are the ways a
        // scroll bar is used now; the arrows were a third of its length.
        const auto* sb = qstyleoption_cast<const QStyleOptionSlider*>(o);
        if (!sb) return QProxyStyle::subControlRect(cc, o, sc, w);
        const bool horizontal = sb->orientation == Qt::Horizontal;
        const QRect r = sb->rect;
        const int length = horizontal ? r.width() : r.height();
        const int range = sb->maximum - sb->minimum;
        int sliderLength = length;
        if (range > 0) {
            sliderLength = int(qint64(sb->pageStep) * length / (range + sb->pageStep));
            // The minimum gives way to a bar shorter than it: clamp() with its
            // low bound above its high one is undefined, and the Arch package's
            // assertions aborted on any scroll bar under 36px (independent review).
            const int least = std::min(pixelMetric(PM_ScrollBarSliderMin, o, w), length);
            sliderLength = std::clamp(sliderLength, least, length);
        }
        const int start = sliderPositionFromValue(sb->minimum, sb->maximum, sb->sliderPosition,
                                                  length - sliderLength, sb->upsideDown);
        // Right to left mirrors a horizontal bar, as Fusion does.
        auto span = [&](int from, int len) {
            return horizontal ? visualRect(sb->direction, r, QRect(r.x() + from, r.y(), len, r.height()))
                              : QRect(r.x(), r.y() + from, r.width(), len);
        };
        switch (sc) {
        case SC_ScrollBarGroove:  return r;
        case SC_ScrollBarSlider:  return span(start, sliderLength);
        case SC_ScrollBarSubPage: return span(0, start);
        case SC_ScrollBarAddPage: return span(start + sliderLength, length - start - sliderLength);
        case SC_ScrollBarAddLine:
        case SC_ScrollBarSubLine:
        case SC_ScrollBarFirst:
        case SC_ScrollBarLast:    return {};
        default: break;
        }
    }

    if (cc == CC_GroupBox) {
        // The title above the panel, not cut into its edge: a group is a
        // surface with a heading, the way a card is.
        const auto* gb = qstyleoption_cast<const QStyleOptionGroupBox*>(o);
        if (gb) {
            QFont f = w ? w->font() : QFont();
            f.setWeight(QFont::DemiBold);
            const QFontMetrics fm(f);
            const int titleH = gb->text.isEmpty() ? 0 : fm.height() + 8;
            switch (sc) {
            case SC_GroupBoxLabel:   // full width; the text is aligned to the leading edge
                return QRect(gb->rect.x() + 4, gb->rect.y(), gb->rect.width() - 8, titleH);
            case SC_GroupBoxFrame:
                return gb->rect.adjusted(0, titleH, 0, 0);
            case SC_GroupBoxContents:
                return gb->rect.adjusted(kGroupPad, titleH + kGroupPad, -kGroupPad, -kGroupPad);
            default: break;
            }
        }
    }

    if (cc == CC_ComboBox && sc == SC_ComboBoxArrow) {
        const QRect r = o->rect;
        return visualRect(o->direction, r, QRect(r.right() - 28, r.y(), 28, r.height()));
    }
    if (cc == CC_ComboBox && sc == SC_ComboBoxEditField) {
        const QRect r = o->rect;
        return visualRect(o->direction, r, QRect(r.x() + 12, r.y(), r.width() - 12 - 28, r.height()));
    }
    if (cc == CC_SpinBox && sc == SC_SpinBoxEditField) {
        // The number sits as far in as a field's text does, not against the
        // rounded edge.
        const QRect field = QProxyStyle::subControlRect(cc, o, sc, w);
        const QRect inset = visualRect(o->direction, o->rect, field).adjusted(kFieldInset - 2, 0, 0, 0);
        return visualRect(o->direction, o->rect, inset);
    }
    return QProxyStyle::subControlRect(cc, o, sc, w);
}

void NapkinStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption* o, QPainter* p,
                                const QWidget* w) const
{
    const QPalette& pal = o->palette;
    switch (pe) {
    case PE_PanelButtonTool: {
        // Round header buttons stand on the window as surfaces; the others are
        // quiet until hovered.
        const QString shape = shapeOf(w);
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = radiusFor(shape, r);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        if (shape == QLatin1String("circle")) p->fillPath(rounded(r, radius), controlFill(pal, w));
        if (isOn(o, State_Sunken) || isOn(o, State_On)) p->fillPath(rounded(r, radius), pressTint(pal));
        else if (isOn(o, State_MouseOver) && isOn(o, State_Enabled)) p->fillPath(rounded(r, radius), hoverTint(pal));
        p->restore();
        if (isOn(o, State_HasFocus) && isOn(o, State_KeyboardFocusChange))
            drawFocusRing(p, r, radius, pal);
        return;
    }
    case PE_PanelButtonCommand:
        return;   // CE_PushButtonBevel draws every button whole
    case PE_FrameDefaultButton:
    case PE_FrameButtonTool:
    case PE_FrameButtonBevel:
    case PE_FrameLineEdit:
    case PE_FrameGroupBox:
    case PE_FrameMenu:
        return;
    case PE_FrameFocusRect: {
        // Buttons, fields, combos, check boxes and shaped controls draw their own
        // ring, so a second one here would double it. Everything else — rows of
        // a list in a dialog, radio buttons, plain tool buttons — gets the same
        // solid ring instead of Fusion's dotted rectangle. Returning nothing for
        // all of them left the Sweep dialog's list with no sign of which row
        // Space would untick (independent review).
        if (qobject_cast<const QPushButton*>(w) || qobject_cast<const QComboBox*>(w)
            || qobject_cast<const QLineEdit*>(w) || (w && w->inherits("QCheckBox"))
            || !shapeOf(w).isEmpty())
            return;
        const bool keyboard = isOn(o, State_KeyboardFocusChange)
            || (w && w->window()->testAttribute(Qt::WA_KeyboardFocusChange));
        if (!keyboard) return;
        // Qt hands over the label's own rectangle, tight to the letters; the
        // ring stands off them rather than touching the text.
        drawFocusRing(p, QRectF(o->rect).adjusted(-4, -2, 4, 2), 6, pal);
        return;
    }
    case PE_PanelLineEdit: {
        const auto* le = qstyleoption_cast<const QStyleOptionFrame*>(o);
        if (le && le->lineWidth <= 0) return;   // a frameless field (inside a spin box)
        const QString shape = shapeOf(w);
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = radiusFor(shape, r);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(r, radius), fieldFill(pal, w));
        if (!isOn(o, State_HasFocus) && isOn(o, State_MouseOver) && isOn(o, State_Enabled))
            p->fillPath(rounded(r, radius), text(pal, kControlHoverAlpha / 2));
        p->restore();
        if (isOn(o, State_HasFocus)) drawFocusRing(p, r, radius, pal);
        return;
    }
    case PE_IndicatorCheckBox: {
        const QRectF r = QRectF(o->rect).adjusted(1.5, 1.5, -1.5, -1.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const bool on = isOn(o, State_On);
        const bool enabled = isOn(o, State_Enabled);
        if (on && enabled) {
            p->fillPath(rounded(r, 4), pal.color(QPalette::Highlight));
            QPainterPath tick;
            tick.moveTo(r.left() + r.width() * 0.25, r.top() + r.height() * 0.52);
            tick.lineTo(r.left() + r.width() * 0.43, r.top() + r.height() * 0.70);
            tick.lineTo(r.left() + r.width() * 0.76, r.top() + r.height() * 0.32);
            p->setPen(QPen(pal.color(QPalette::HighlightedText), 1.8, Qt::SolidLine,
                           Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(tick);
        } else {
            p->fillPath(rounded(r, 4), fieldFill(pal, w));
            p->setPen(QPen(text(pal, enabled ? kControlEdgeAlpha : kControlEdgeAlpha / 2), 1.0));
            p->drawPath(rounded(r, 4));
            if (on) {   // disabled but checked: still says so, in grey
                p->setPen(QPen(text(pal, kTextTertiary), 1.6));
                p->drawLine(QPointF(r.left() + 4, r.center().y()), QPointF(r.right() - 4, r.center().y()));
            }
        }
        p->restore();
        if (isOn(o, State_HasFocus) && isOn(o, State_KeyboardFocusChange))
            drawFocusRing(p, QRectF(o->rect).adjusted(-2, -2, 2, 2), 6, pal);
        return;
    }
    case PE_PanelMenu: {
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(r, kMenuRadius), pal.color(QPalette::Base));
        // Breeze's shadow went with Breeze, and a menu (a surface) over cards
        // (surfaces) needs an edge that reads on its own.
        p->setPen(QPen(text(pal, kMenuEdgeAlpha), 1.0));
        p->drawPath(rounded(r, kMenuRadius));
        p->restore();
        return;
    }
    case PE_Frame: {
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(QPen(text(pal, kHairline), 1.0));
        p->setBrush(Qt::NoBrush);
        p->drawPath(rounded(r, kControlRadius));
        p->restore();
        return;
    }
    case PE_PanelTipLabel: {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillRect(o->rect, pal.color(QPalette::ToolTipBase));
        p->restore();
        return;
    }
    case PE_PanelItemViewItem: {
        // A list inside a dialog: the same rounded, quiet states as the menus.
        const QRectF r = QRectF(o->rect).adjusted(2, 1, -2, -1);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (shapeOf(w) == QLatin1String("nav")) {
            // A list of places (Settings' pages): the chosen one is cut out of
            // the surface in the window's colour, as the main window's sidebar
            // and the Home / Trash switch do it.
            if (isOn(o, State_Selected)) p->fillPath(rounded(r, kControlRadius), pal.color(QPalette::Window));
            else if (isOn(o, State_MouseOver)) p->fillPath(rounded(r, kControlRadius), hoverTint(pal));
            p->restore();
            return;
        }
        if (isOn(o, State_Selected))
            p->fillPath(rounded(r, 6), highlight(pal, isLightTheme(pal) ? 40 : 60));
        else if (isOn(o, State_MouseOver))
            p->fillPath(rounded(r, 6), hoverTint(pal));
        p->restore();
        return;
    }
    default: break;
    }
    QProxyStyle::drawPrimitive(pe, o, p, w);
}

void NapkinStyle::drawControl(ControlElement ce, const QStyleOption* o, QPainter* p,
                              const QWidget* w) const
{
    const QPalette& pal = o->palette;
    switch (ce) {
    case CE_PushButtonBevel: {
        const auto* b = qstyleoption_cast<const QStyleOptionButton*>(o);
        if (!b) break;
        const QString shape = shapeOf(w);
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = radiusFor(shape, r);
        const bool flat = b->features & QStyleOptionButton::Flat;
        // The dialog's real default, not whichever autoDefault button has focus:
        // Qt marks that one DefaultButton too, so Tab to Cancel painted Cancel
        // as the primary action.
        const auto* pb = qobject_cast<const QPushButton*>(w);
        const bool isDefault = (b->features & QStyleOptionButton::DefaultButton)
                               && (!pb || pb->isDefault());
        const bool enabled = isOn(o, State_Enabled);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (shape == QLatin1String("segment")) {
            // The chosen option is a pill of the window's colour set into the
            // lighter track, as the mockup has it — a cut-out, not a highlight.
            if (isOn(o, State_On)) {
                const QPainterPath pill = rounded(r.adjusted(3, 3, -3, -3), radius - 3);
                p->fillPath(pill, pal.color(QPalette::Window));
                p->setPen(QPen(text(pal, kHairline), 1.0));
                p->drawPath(pill);
            }
            else if (isOn(o, State_MouseOver) && enabled) p->fillPath(rounded(r.adjusted(3, 3, -3, -3), radius - 3), hoverTint(pal));
        } else if (isDefault && !flat && enabled) {
            QColor fill = pal.color(QPalette::Highlight);
            if (isOn(o, State_Sunken)) fill = fill.darker(115);
            else if (isOn(o, State_MouseOver)) fill = fill.lighter(108);
            p->fillPath(rounded(r, radius), fill);
        } else {
            if (!flat || shape == QLatin1String("pill"))
                p->fillPath(rounded(r, radius), controlFill(pal, w));
            if (isOn(o, State_Sunken) || isOn(o, State_On)) p->fillPath(rounded(r, radius), pressTint(pal));
            else if (isOn(o, State_MouseOver) && enabled) p->fillPath(rounded(r, radius), hoverTint(pal));
        }
        p->restore();
        if (isOn(o, State_HasFocus) && isOn(o, State_KeyboardFocusChange))
            drawFocusRing(p, r, radius, pal);
        return;
    }
    case CE_PushButtonLabel: {
        const auto* b = qstyleoption_cast<const QStyleOptionButton*>(o);
        if (!b) break;
        QStyleOptionButton copy(*b);
        copy.state &= ~State_HasFocus;   // no dotted rectangle around the words
        const QString shape = shapeOf(w);
        const auto* pb = qobject_cast<const QPushButton*>(w);
        if ((b->features & QStyleOptionButton::DefaultButton) && (!pb || pb->isDefault())
            && !(b->features & QStyleOptionButton::Flat) && isOn(o, State_Enabled)) {
            copy.palette.setColor(QPalette::ButtonText, pal.color(QPalette::HighlightedText));
        } else if (shape == QLatin1String("segment") && !isOn(o, State_On)) {
            copy.palette.setColor(QPalette::ButtonText, text(pal, kTextSecondary));
        }
        if (shape == QLatin1String("segment") && isOn(o, State_On)) {
            // Chosen is said in weight as well as fill: the fills differ by
            // only 1.18:1, which is a hint, not a signal.
            p->save();
            QFont f = p->font();
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            QProxyStyle::drawControl(ce, &copy, p, w);
            p->restore();
            return;
        }
        QProxyStyle::drawControl(ce, &copy, p, w);
        return;
    }
    case CE_MenuItem: {
        const auto* mi = qstyleoption_cast<const QStyleOptionMenuItem*>(o);
        if (!mi) break;
        if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
            p->save();
            if (mi->text.isEmpty()) {
                const int y = mi->rect.center().y();
                p->setPen(QPen(text(pal, kHairline), 1.0));
                p->drawLine(mi->rect.left() + 10, y, mi->rect.right() - 10, y);
            } else {
                // A section heading: the small caps label the list uses.
                QFont f = p->font();
                f = scaledBy(f, kTypeCaption, QFont::DemiBold);
                p->setFont(f);
                p->setPen(text(pal, kTextTertiary));
                p->drawText(mi->rect.adjusted(14, 4, -10, 0), Qt::AlignLeft | Qt::AlignVCenter,
                            mi->text.toUpper());
            }
            p->restore();
            return;
        }
        // The active item is the accent, rounded, with its own text colour. A
        // tint of the text colour measured 1.14:1 — barely there, and with no
        // menu bar this menu is how a keyboard user reaches everything.
        QStyleOptionMenuItem copy(*mi);
        if (isOn(o, State_Selected) && isOn(o, State_Enabled)) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing, true);
            p->fillPath(rounded(QRectF(mi->rect).adjusted(4, 1, -4, -1), 6),
                        pal.color(QPalette::Highlight));
            p->restore();
            const QColor on = pal.color(QPalette::HighlightedText);
            for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
                copy.palette.setColor(role, on);
        }
        copy.state &= ~State_Selected;   // Fusion's square highlight is not drawn
        QProxyStyle::drawControl(ce, &copy, p, w);
        return;
    }
    case CE_ItemViewItem: {
        // PE_PanelItemViewItem tints a selected row rather than filling it, so
        // the text stays the text colour: HighlightedText is chosen for an
        // opaque accent fill, and on a tint it was near-black on dark.
        const auto* vi = qstyleoption_cast<const QStyleOptionViewItem*>(o);
        if (!vi) break;
        QStyleOptionViewItem copy(*vi);
        copy.palette.setColor(QPalette::HighlightedText, pal.color(QPalette::Text));
        if (shapeOf(w) == QLatin1String("nav")) {
            // Chosen is said in weight too, not by a 1.18:1 fill alone.
            if (isOn(o, State_Selected)) copy.font.setWeight(QFont::DemiBold);
        }
        QProxyStyle::drawControl(ce, &copy, p, w);
        return;
    }
    case CE_MenuEmptyArea:
    case CE_MenuBarEmptyArea:
        return;
    case CE_Splitter:
        return;   // a gap, not a groove; the cursor says it can be dragged
    default: break;
    }
    QProxyStyle::drawControl(ce, o, p, w);
}

void NapkinStyle::drawComplexControl(ComplexControl cc, const QStyleOptionComplex* o,
                                     QPainter* p, const QWidget* w) const
{
    const QPalette& pal = o->palette;
    switch (cc) {
    case CC_ScrollBar: {
        const auto* sb = qstyleoption_cast<const QStyleOptionSlider*>(o);
        if (!sb) break;
        const QRect slider = subControlRect(cc, o, SC_ScrollBarSlider, w);
        if (sb->maximum <= sb->minimum || slider.isEmpty()) return;
        const bool horizontal = sb->orientation == Qt::Horizontal;
        const bool active = isOn(o, State_MouseOver) || (sb->activeSubControls & SC_ScrollBarSlider);
        const qreal thickness = active ? kScrollBarExtent - 2 : 6;
        QRectF r = slider;
        if (horizontal) r = QRectF(r.x() + 2, r.bottom() - thickness - 1, r.width() - 4, thickness);
        else            r = QRectF(r.right() - thickness - 1 + 1, r.y() + 2, thickness, r.height() - 4);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(r, thickness / 2.0),
                    text(pal, (sb->state & State_Sunken) ? 170 : active ? 150 : kScrollThumbAlpha));
        p->restore();
        return;
    }
    case CC_ComboBox: {
        const auto* cb = qstyleoption_cast<const QStyleOptionComboBox*>(o);
        if (!cb) break;
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(r, kControlRadius), cb->editable ? fieldFill(pal, w) : controlFill(pal, w));
        if (isOn(o, State_On)) p->fillPath(rounded(r, kControlRadius), pressTint(pal));
        else if (isOn(o, State_MouseOver) && isOn(o, State_Enabled)) p->fillPath(rounded(r, kControlRadius), hoverTint(pal));
        p->restore();
        const QRect arrow = subControlRect(cc, o, SC_ComboBoxArrow, w);
        drawChevron(p, QRectF(arrow).adjusted(6, 6, -8, -6), true,
                    text(pal, isOn(o, State_Enabled) ? kTextSecondary : kTextTertiary));
        if (isOn(o, State_HasFocus) && isOn(o, State_KeyboardFocusChange))
            drawFocusRing(p, r, kControlRadius, pal);
        return;
    }
    case CC_SpinBox: {
        const auto* sp = qstyleoption_cast<const QStyleOptionSpinBox*>(o);
        if (!sp) break;
        const QRectF r = QRectF(o->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(r, kControlRadius), fieldFill(pal, w));
        p->restore();
        if (sp->buttonSymbols != QAbstractSpinBox::NoButtons) {
            const QRect up = subControlRect(cc, o, SC_SpinBoxUp, w);
            const QRect down = subControlRect(cc, o, SC_SpinBoxDown, w);
            auto arrowColour = [&](SubControl s, QAbstractSpinBox::StepEnabledFlag f) {
                if (!(sp->stepEnabled & f) || !isOn(o, State_Enabled)) return text(pal, kTextTertiary / 2);
                return (sp->activeSubControls & s) ? pal.color(QPalette::Text) : text(pal, kTextSecondary);
            };
            drawChevron(p, QRectF(up).adjusted(2, 1, -4, 2), false,
                        arrowColour(SC_SpinBoxUp, QAbstractSpinBox::StepUpEnabled));
            drawChevron(p, QRectF(down).adjusted(2, -2, -4, -1), true,
                        arrowColour(SC_SpinBoxDown, QAbstractSpinBox::StepDownEnabled));
        }
        if (isOn(o, State_HasFocus)) drawFocusRing(p, r, kControlRadius, pal);
        return;
    }
    case CC_GroupBox: {
        const auto* gb = qstyleoption_cast<const QStyleOptionGroupBox*>(o);
        if (!gb) break;
        const QRectF frame = QRectF(subControlRect(cc, o, SC_GroupBoxFrame, w)).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->fillPath(rounded(frame, kCardRadius), pal.color(QPalette::Base));
        if (!gb->text.isEmpty()) {
            QFont f = p->font();
            f.setWeight(QFont::DemiBold);
            p->setFont(f);
            p->setPen(pal.color(QPalette::WindowText));
            p->drawText(subControlRect(cc, o, SC_GroupBoxLabel, w), Qt::AlignLeading | Qt::AlignVCenter, gb->text);
        }
        p->restore();
        return;
    }
    case CC_ToolButton: {
        const auto* tb = qstyleoption_cast<const QStyleOptionToolButton*>(o);
        if (!tb) break;
        QStyleOptionToolButton copy(*tb);
        copy.features &= ~QStyleOptionToolButton::HasMenu;   // see PM_MenuButtonIndicator
        QProxyStyle::drawComplexControl(cc, &copy, p, w);
        return;
    }
    default: break;
    }
    QProxyStyle::drawComplexControl(cc, o, p, w);
}

}  // namespace napkin
