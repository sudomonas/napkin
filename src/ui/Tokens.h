#pragma once
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPalette>
#include <algorithm>
#include <cmath>

// The visual system, in one place. Every alpha here is a measured contrast
// threshold composited over QPalette::Base against Breeze Light, the harsher of
// the two themes — not a taste choice. Anything readable stays at or above 161.
namespace napkin::tokens {

// --- text alphas -------------------------------------------------------------
inline constexpr int kTextPrimary   = 255;  // >= 15:1
inline constexpr int kTextSecondary = 170;  // 5.06:1  — secondary lines, meta
inline constexpr int kTextTertiary  = 161;  // 4.51:1  — timestamps, captions,
                                            //           section labels, and
                                            //           placeholders, which are
                                            //           text and get no exemption

// --- non-text ----------------------------------------------------------------
// Only what something actually paints from. An audit found that over half of
// this file was unreferenced while the live values were literals elsewhere that
// disagreed with it — a design system nothing consults is not a design system,
// it is a second thing to forget to change.
inline constexpr int kHairline   = 36;   // dividers, image edges
inline constexpr int kCardActive = 178;  // the list's hovered / current row

// The wash behind a selected card. Measured, not chosen: this is how much of
// the accent a card can take before the text on it loses contrast, and it
// differs by theme because the accent sits on a light or a dark Base.
inline constexpr int kFillSelectedLight = 20;
inline constexpr int kFillSelectedDark  = 34;

// --- spacing, on a 4px scale -------------------------------------------------
inline constexpr int kGapTight  = 8;
inline constexpr int kPadX      = 32;
inline constexpr int kPadTop    = 28;

// --- geometry ----------------------------------------------------------------
// Cards, not a column. A board of pasted things wants a uniform width and each
// card's own height; a single 780px column gave a three-word note a 780px row.
inline constexpr int kCardMinWidth = 280;
inline constexpr int kCardMaxWidth = 460;

// The width a column WANTS, as opposed to the narrowest it will tolerate.
//
// Packing as many columns as fit at kCardMinWidth means the reading measure
// gets worse the bigger the window is: a 1920px board gave five 280px columns
// of about 34 characters, while a 1280px board gave 417px columns. Aiming at a
// target and then widening to fill inverts that — 1920 now gives four columns
// of ~365px. A board is for reading, and a line of 34 characters is not a
// reward for having a large screen.
inline constexpr int kCardTargetWidth = 360;

// Enough for roughly 18 lines of body text or a landscape screenshot. Past this
// a card would own the board, so it clips with a fade and opens on double-click.
inline constexpr int kCardMaxHeight = 420;

// A card is never shorter than this, so a one-word paste is still a card you can
// aim at rather than a sliver. Every card's size depends only on its own
// content: nothing here is derived from what the neighbours are doing.
// One line of body text plus the chrome. Deliberately not padded out to
// something rounder: a min height that exceeds what a short card needs makes
// the board lie about how much is in it.
inline constexpr int kCardMinHeight = 88;

// Enough characters to overflow kCardMaxHeight at any column width we allow, so
// measuring beyond this cannot change a card's height.
inline constexpr int kMeasureLimit = 2048;

// Card chrome.
inline constexpr int kCardPad      = 16;   // content inset
inline constexpr int kCardFooterH  = 28;   // the copy action and the timestamp,
                                          // at the default font — use
                                          // footerHeight() for the real one
inline constexpr int kCardGap      = 20;

// The board width at which BoardLayout::rebuild() settles on exactly `columns`
// columns — the inverse of the arithmetic there, kept beside the constants it
// divides by so the two cannot drift apart.
//
// The divisor is kCardTargetWidth, not kCardMinWidth. A board wide enough for
// two 280px columns still draws one, so sizing a window off the minimum would
// promise a second column and not deliver it.
inline constexpr int boardWidthForColumns(int columns)
{
    return kPadX * 2 + columns * kCardTargetWidth + (columns - 1) * kCardGap;
}

// A card's edge is a meaningful affordance, so it obeys the same 3:1 floor as
// everything else here. An earlier value of 62 was 1.63:1 in light — declared
// "quiet", measured invisible, and contradicting the constant four lines above
// it. Dark needs less to read as an edge, so it gets less.
inline constexpr int kCardBorderLight = 128;  // 3.09:1
inline constexpr int kCardBorderDark  = 108;  // 4.00:1
inline constexpr int kCardBorderHoverBoost = 46;

// QPalette::Highlight is chosen by the theme for *fills*, where the text on top
// carries the contrast. Used as a hairline against Base it is often far below
// 3:1 — Breeze Light's is 2.1:1 at full strength. Darken (or lighten, in a dark
// theme) until it genuinely reads as an edge.
// WCAG relative luminance and the ratio between two opaque colours. Exported
// because more than one place needs to CHOOSE a colour by contrast rather than
// assert one after the fact.
inline qreal relativeLuminance(const QColor& c)
{
    auto channel = [](int v) {
        const qreal s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green())
           + 0.0722 * channel(c.blue());
}

inline qreal contrastRatio(const QColor& a, const QColor& b)
{
    const qreal la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// What to write on top of a fill. Measured, not guessed: choosing by
// QColor::lightness() looked equivalent and put white on a mid green at 2.9:1,
// because HSL lightness is not luminance — green carries most of the visible
// energy and blue almost none, which a lightness value does not know.
inline QColor textOn(const QColor& fill)
{
    const QColor light(252, 252, 252), dark(35, 38, 41);
    return contrastRatio(light, fill) >= contrastRatio(dark, fill) ? light : dark;
}

inline QColor readableAccent(const QPalette& pal, qreal strength = 1.0)
{
    const QColor base = pal.color(QPalette::Base);
    const bool light = base.lightness() > 128;
    QColor accent = pal.color(QPalette::Highlight);

    auto relLum = [](const QColor& c) {
        auto ch = [](int v) {
            const qreal s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * ch(c.red()) + 0.7152 * ch(c.green()) + 0.0722 * ch(c.blue());
    };
    auto ratio = [&](const QColor& a, const QColor& b) {
        const qreal la = relLum(a), lb = relLum(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };

    for (int i = 0; i < 24 && ratio(accent, base) < 3.0; ++i)
        accent = light ? accent.darker(112) : accent.lighter(112);

    if (strength < 1.0) {
        // A softer variant for "selected but not editing", still above 3:1
        // because it only ever moves toward the accent, never back to Base.
        accent.setAlphaF(std::max(0.75, strength));
    }
    return accent;
}

inline int cardBorderAlpha(const QPalette& pal, bool hovered)
{
    const int base = pal.color(QPalette::Window).lightness() > 128 ? kCardBorderLight
                                                                  : kCardBorderDark;
    return hovered ? std::min(255, base + kCardBorderHoverBoost) : base;
}

inline constexpr int kRailWidth = 3;

// --- radii --------------------------------------------------------------------
// Two scales, named for what they are on rather than left to be guessed at. The
// old file declared "nothing above 8" four lines above a card that paints 10,
// which meant the rule and the pixels had never agreed.
inline constexpr int kCardRadius = 10;  // a card on the board, and the list's rows
inline constexpr int kInsetRadius = 6;  // something drawn INSIDE a card: the chip

inline QColor text(const QPalette& pal, int alpha)
{
    QColor c = pal.color(QPalette::Text);
    c.setAlpha(alpha);
    return c;
}

inline QColor highlight(const QPalette& pal, int alpha)
{
    QColor c = pal.color(QPalette::Highlight);
    c.setAlpha(alpha);
    return c;
}

inline bool isLightTheme(const QPalette& pal)
{
    return pal.color(QPalette::Window).lightness() > 128;
}

// The footer holds text, so its height follows the font. Fixed at 28px it fit
// at the default size and clipped the card's own content at 200%: the board
// derives a card's chrome from this number, so a stale one is a card whose
// content area is smaller than the thing it was measured to hold.
inline int footerHeight(const QFont& font)
{
    return std::max(kCardFooterH, QFontMetrics(font).height() + 10);
}

// Everything above the content: padding, the footer, and the gap to it.
inline int cardChromeHeight(const QFont& font)
{
    return kCardPad * 2 - 6 + footerHeight(font) + kGapTight;
}

// --- the type scale ----------------------------------------------------------
// Ratios, not point offsets.
//
// Every size used to be "base ± n points", which is a fixed *proportion* only
// at one base size. A -1.5pt caption is 15% smaller at 10pt and 6% smaller at
// 24pt, so at the 150% and 200% text settings the whole card collapsed into one
// undifferentiated size — and the people who need large type are exactly the
// people who need hierarchy most. Point offsets also do not survive a change of
// typeface, where point sizes are not comparable between faces.
inline constexpr qreal kTypeMicro   = 0.70;  // the GIF badge; a glyph, not prose
inline constexpr qreal kTypeCaption = 0.85;  // timestamps, captions, section labels
inline constexpr qreal kTypeBody    = 1.00;
inline constexpr qreal kTypeLead    = 1.15;  // the line that leads a small block
inline constexpr qreal kTypeTitle   = 1.45;  // an empty state's headline
inline constexpr qreal kTypeDisplay = 2.40;  // the wordmark, once, on first run

inline QFont scaledBy(const QFont& base, qreal ratio, int weight = -1)
{
    QFont f = base;
    f.setPointSizeF(std::max(7.0, base.pointSizeF() * ratio));
    if (weight >= 0) f.setWeight(QFont::Weight(weight));
    return f;
}

// Kept for the handful of places that genuinely want an absolute nudge rather
// than a step on the scale. New call sites should use scaledBy().
inline QFont scaled(const QFont& base, qreal deltaPt, int weight = -1)
{
    QFont f = base;
    f.setPointSizeF(std::max(7.0, base.pointSizeF() + deltaPt));
    if (weight >= 0) f.setWeight(QFont::Weight(weight));
    return f;
}

}  // namespace napkin::tokens
