#pragma once
#include <QColor>
#include <QIcon>

class QPainter;
class QPalette;
class QRect;

namespace napkin::icons {

// Drawn rather than shipped as assets: two glyphs at one size do not justify an
// icon theme, and vector paths stay crisp at any scale factor without a @2x set.
void drawPin(QPainter* p, const QRect& box, const QColor& colour);
void drawKeep(QPainter* p, const QRect& box, const QColor& colour);

// Two offset sheets: the universal copy glyph.
void drawCopy(QPainter* p, const QRect& box, const QColor& colour);

// Two interlocking links, for the chip a lone URL renders as.
void drawLink(QPainter* p, const QRect& box, const QColor& colour);

// A lidded can with two ribs: the trash, for the toolbar's Trash button.
void drawTrash(QPainter* p, const QRect& box, const QColor& colour);

// A plus and a gear, as fallbacks for the library's New and Settings icons.
void drawPlus(QPainter* p, const QRect& box, const QColor& colour);
void drawGear(QPainter* p, const QRect& box, const QColor& colour);

// Three dots in a row: "more actions".
void drawMore(QPainter* p, const QRect& box, const QColor& colour);

// Three bars ("the menu") and a magnifier, as fallbacks for Lucide's menu and
// search — the header's menu button and the search field's glyph.
void drawMenu(QPainter* p, const QRect& box, const QColor& colour);
void drawSearch(QPainter* p, const QRect& box, const QColor& colour);

// One of the glyphs above as a QIcon for a button, drawn in the palette's
// button-text colour (and dimmed when disabled) at the given device pixel
// ratio, so it stays crisp and follows the theme. Rebuild it on a palette
// change: the colour is baked in.
using GlyphPainter = void (*)(QPainter*, const QRect&, const QColor&);
QIcon glyphIcon(GlyphPainter draw, const QPalette& palette, int size, qreal dpr);

// A toolbar icon from the bundled Lucide set (resources/icons/lucide), drawn
// in the palette's button-text colour. Lucide strokes with currentColor, which
// Qt's SVG renderer cannot resolve against a palette, so the colour is written
// into the SVG before it is rendered. Falls back to `fallback`, a drawn glyph,
// if the SVG image plugin is unavailable — a blank button is worse than a
// hand-drawn one.
QIcon libraryIcon(const char* name, const QPalette& palette, int size, qreal dpr,
                  GlyphPainter fallback);

inline constexpr int kGlyphSize = 13;

}  // namespace napkin::icons
