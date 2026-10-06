#include "Icons.h"
#include <cmath>
#include <QImageReader>
#include <QFile>
#include <QBuffer>
#include <algorithm>
#include <QPalette>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QRect>

namespace napkin::icons {

// A pushpin: flat head, a crossbar, a tapered shaft and a point.
//
// The first attempt was a filled circle with a straight stem, which at 13px is
// the universal magnifying-glass idiom — and would have collided head-on with
// the search field arriving in the header. The crossbar and the point are what
// make it read as a pin rather than a lens.
void drawPin(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    const qreal cx = r.center().x(), top = r.top() + r.height() * 0.10;
    const qreal w = r.width(), h = r.height();

    QPainterPath pin;
    pin.moveTo(cx - w * 0.26, top);                  // flat head
    pin.lineTo(cx + w * 0.26, top);
    pin.lineTo(cx + w * 0.17, top + h * 0.16);
    pin.lineTo(cx + w * 0.38, top + h * 0.42);       // crossbar, right wing
    pin.lineTo(cx + w * 0.07, top + h * 0.50);
    pin.lineTo(cx,            top + h * 0.90);       // point
    pin.lineTo(cx - w * 0.07, top + h * 0.50);
    pin.lineTo(cx - w * 0.38, top + h * 0.42);       // crossbar, left wing
    pin.lineTo(cx - w * 0.17, top + h * 0.16);
    pin.closeSubpath();
    p->fillPath(pin, colour);
    p->restore();
}

// A bookmark: what you mark so it is still there later. Deliberately not a
// padlock — Keep is retention, not security, and the icon must not promise
// otherwise (SPEC.md §3).
void drawKeep(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    const qreal w = r.width() * 0.62, h = r.height() * 0.78;
    const qreal x = r.center().x() - w / 2, y = r.center().y() - h / 2;

    QPainterPath mark;
    mark.moveTo(x, y);
    mark.lineTo(x + w, y);
    mark.lineTo(x + w, y + h);
    mark.lineTo(x + w / 2, y + h * 0.66);
    mark.lineTo(x, y + h);
    mark.closeSubpath();
    p->fillPath(mark, colour);
    p->restore();
}

// Two offset sheets, which is what every toolbar in the world means by copy.
void drawCopy(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    const qreal w = r.width(), h = r.height();
    QPen pen(colour, std::max(1.0, w * 0.085));
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(QRectF(r.left() + w * 0.06, r.top() + w * 0.06,
                              w * 0.62, h * 0.62), w * 0.10, w * 0.10);
    p->drawRoundedRect(QRectF(r.left() + w * 0.32, r.top() + w * 0.32,
                              w * 0.62, h * 0.62), w * 0.10, w * 0.10);
    p->restore();
}

void drawLink(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    const qreal w = r.width(), h = r.height();
    QPen pen(colour, std::max(1.2, w * 0.10));
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);

    // Two capsules on a diagonal with the bar between them: a chain link, at a
    // size where a more literal drawing turns to mush.
    const qreal cw = w * 0.46, ch = h * 0.30;
    QRectF upper(r.left() + w * 0.06, r.top() + h * 0.10, cw, ch);
    QRectF lower(r.left() + w * 0.48, r.top() + h * 0.60, cw, ch);
    p->drawRoundedRect(upper, ch / 2, ch / 2);
    p->drawRoundedRect(lower, ch / 2, ch / 2);
    p->drawLine(QPointF(r.left() + w * 0.36, r.top() + h * 0.56),
                QPointF(r.left() + w * 0.64, r.top() + h * 0.44));
    p->restore();
}

void drawTrash(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    const qreal w = r.width(), h = r.height();
    QPen pen(colour, std::max(1.0, w * 0.085));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    // Lid and handle.
    p->drawLine(QPointF(r.left() + w * 0.08, r.top() + h * 0.22),
                QPointF(r.right() - w * 0.08, r.top() + h * 0.22));
    p->drawLine(QPointF(r.left() + w * 0.38, r.top() + h * 0.08),
                QPointF(r.right() - w * 0.38, r.top() + h * 0.08));
    // The can, narrowing slightly towards the bottom.
    QPainterPath can;
    can.moveTo(r.left() + w * 0.18, r.top() + h * 0.22);
    can.lineTo(r.left() + w * 0.25, r.bottom() - h * 0.06);
    can.lineTo(r.right() - w * 0.25, r.bottom() - h * 0.06);
    can.lineTo(r.right() - w * 0.18, r.top() + h * 0.22);
    p->drawPath(can);
    // Two ribs, so it reads as a bin and not as a cup.
    p->drawLine(QPointF(r.left() + w * 0.42, r.top() + h * 0.38),
                QPointF(r.left() + w * 0.43, r.bottom() - h * 0.20));
    p->drawLine(QPointF(r.right() - w * 0.42, r.top() + h * 0.38),
                QPointF(r.right() - w * 0.43, r.bottom() - h * 0.20));
    p->restore();
}

void drawPlus(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    QPen pen(colour, std::max(1.0, r.width() * 0.10));
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->drawLine(QPointF(r.center().x(), r.top() + r.height() * 0.2), QPointF(r.center().x(), r.bottom() - r.height() * 0.2));
    p->drawLine(QPointF(r.left() + r.width() * 0.2, r.center().y()), QPointF(r.right() - r.width() * 0.2, r.center().y()));
    p->restore();
}

void drawGear(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    QPen pen(colour, std::max(1.0, r.width() * 0.09));
    pen.setCapStyle(Qt::RoundCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    const QPointF c = r.center();
    const qreal outer = r.width() * 0.42, ring = r.width() * 0.28, hub = r.width() * 0.11;
    for (int k = 0; k < 8; ++k) {
        const qreal a = k * 3.14159265 / 4;
        p->drawLine(c + QPointF(std::cos(a), std::sin(a)) * ring, c + QPointF(std::cos(a), std::sin(a)) * outer);
    }
    p->drawEllipse(c, ring, ring);
    p->drawEllipse(c, hub, hub);
    p->restore();
}

void drawMore(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    const qreal d = std::max(2.0, r.width() * 0.17);
    p->setPen(Qt::NoPen);
    p->setBrush(colour);
    for (int k = -1; k <= 1; ++k)
        p->drawEllipse(QPointF(r.center().x() + k * r.width() * 0.34, r.center().y()), d / 2, d / 2);
    p->restore();
}

void drawMenu(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    p->setPen(QPen(colour, std::max(1.2, r.width() * 0.085), Qt::SolidLine, Qt::RoundCap));
    for (qreal f : {0.21, 0.5, 0.79})   // Lucide's 5, 12 and 19 of 24
        p->drawLine(QPointF(r.left() + r.width() * 0.17, r.top() + r.height() * f),
                    QPointF(r.right() - r.width() * 0.17, r.top() + r.height() * f));
    p->restore();
}

void drawSearch(QPainter* p, const QRect& box, const QColor& colour)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(box);
    p->setPen(QPen(colour, std::max(1.2, r.width() * 0.085), Qt::SolidLine, Qt::RoundCap));
    p->setBrush(Qt::NoBrush);
    const QPointF c(r.left() + r.width() * 0.46, r.top() + r.height() * 0.46);
    const qreal rad = r.width() * 0.33;
    p->drawEllipse(c, rad, rad);
    p->drawLine(QPointF(c.x() + rad * 0.72, c.y() + rad * 0.72),
                QPointF(r.right() - r.width() * 0.12, r.bottom() - r.height() * 0.12));
    p->restore();
}

QIcon glyphIcon(GlyphPainter draw, const QPalette& palette, int size, qreal dpr)
{
    auto render = [&](const QColor& colour) {
        QPixmap pm(QSize(size, size) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        draw(&p, QRect(0, 0, size, size), colour);
        return pm;
    };
    QIcon icon;
    icon.addPixmap(render(palette.color(QPalette::ButtonText)), QIcon::Normal);
    icon.addPixmap(render(palette.color(QPalette::ButtonText)), QIcon::Active);
    icon.addPixmap(render(palette.color(QPalette::Disabled, QPalette::ButtonText)), QIcon::Disabled);
    return icon;
}

namespace {
QPixmap renderSvg(const QByteArray& svg, const QColor& colour, int size, qreal dpr)
{
    QByteArray coloured = svg;
    coloured.replace("currentColor", colour.name().toLatin1());
    QBuffer buffer(&coloured);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "svg");
    reader.setScaledSize(QSize(size, size) * dpr);
    QImage image = reader.read();
    if (image.isNull()) return {};
    // A colour's name is its RGB alone, so a translucent colour — a quiet
    // glyph at a text alpha — rendered fully opaque. The alpha goes on after.
    if (colour.alpha() < 255) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage faded(image.size(), QImage::Format_ARGB32_Premultiplied);
        faded.fill(Qt::transparent);
        QPainter p(&faded);
        p.setOpacity(colour.alphaF());
        p.drawImage(0, 0, image);
        p.end();
        image = faded;
    }
    QPixmap pm = QPixmap::fromImage(image);
    pm.setDevicePixelRatio(dpr);
    return pm;
}
}  // namespace

QIcon libraryIcon(const char* name, const QPalette& palette, int size, qreal dpr,
                  GlyphPainter fallback)
{
    QFile file(QStringLiteral(":/resources/icons/lucide/%1.svg").arg(QLatin1String(name)));
    const QByteArray svg = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    const QPixmap normal = renderSvg(svg, palette.color(QPalette::ButtonText), size, dpr);
    if (normal.isNull()) return glyphIcon(fallback, palette, size, dpr);

    QIcon icon;
    icon.addPixmap(normal, QIcon::Normal);
    icon.addPixmap(normal, QIcon::Active);
    icon.addPixmap(renderSvg(svg, palette.color(QPalette::Disabled, QPalette::ButtonText), size, dpr),
                   QIcon::Disabled);
    return icon;
}

}  // namespace napkin::icons
