#pragma once
#include <QColor>
#include <QRgb>
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
inline constexpr int kHairline   = 36;   // dividers, and the link chip's edge
// The edge drawn around a picture, in the list and on the board. Heavier than a
// hairline on purpose: its job is to separate an image from a card of the same
// colour — a dark screenshot on a dark card read as a hole — so it has to be
// seen. Both places said 60 as a literal while kHairline claimed to be this.
inline constexpr int kImageEdge  = 60;
inline constexpr int kCardActive = 178;  // the list's hovered / current row

// The wash behind a selected card. Measured, not chosen: this is how much of
// the accent a card can take before the text on it loses contrast, and it
// differs by theme because the accent sits on a light or a dark Base.
inline constexpr int kFillSelectedLight = 20;
inline constexpr int kFillSelectedDark  = 34;

// --- theme colours ----------------------------------------------------------
// Napkin's Light and Dark, every role a widget can paint with. They were literals
// inside SettingsDialog's palette builder while textOn() below kept its own copy
// of two of them; one colour, two definitions. The QPalette is built from these.
// The accent is not here: it belongs to the user (SPEC.md §7, Theming).
struct ThemeColors {
    QRgb window, windowText, base, alternateBase, text, button, buttonText,
         brightText, toolTipBase, toolTipText,
         light, midlight, mid, dark, shadow,     // the bevel roles styles draw with
         link, linkVisited,
         disabledText, disabledHighlight;
};

// Warm greys, from the 2026-10-06 mockup: the window is the desk (#E0DFDD),
// and everything you touch — cards, the sidebar, the search field, the header's
// buttons — is a lighter surface on it (#F2F1EF). Button is the surface, so a
// control looks like the thing it sits beside. Text is darker than the mockup's
// so tertiary text (α161) still reaches 4.5:1 on the window as well as on a card.
inline constexpr ThemeColors kLightColors{
    .window = qRgb(224, 223, 221), .windowText = qRgb(20, 19, 18),
    .base = qRgb(242, 241, 239), .alternateBase = qRgb(233, 232, 229),
    .text = qRgb(20, 19, 18),
    .button = qRgb(242, 241, 239), .buttonText = qRgb(20, 19, 18),
    .brightText = qRgb(255, 255, 255),
    .toolTipBase = qRgb(42, 41, 39), .toolTipText = qRgb(242, 241, 239),
    .light = qRgb(255, 255, 255), .midlight = qRgb(247, 246, 244),
    .mid = qRgb(201, 199, 195), .dark = qRgb(142, 140, 136), .shadow = qRgb(74, 73, 70),
    .link = qRgb(38, 104, 172), .linkVisited = qRgb(110, 90, 158),
    .disabledText = qRgb(142, 140, 136), .disabledHighlight = qRgb(211, 209, 205),
};

// The same structure at night: the desk is darkest, surfaces step up from it.
inline constexpr ThemeColors kDarkColors{
    .window = qRgb(27, 26, 25), .windowText = qRgb(236, 235, 232),
    .base = qRgb(38, 37, 35), .alternateBase = qRgb(47, 46, 44),
    .text = qRgb(236, 235, 232),
    .button = qRgb(38, 37, 35), .buttonText = qRgb(236, 235, 232),
    .brightText = qRgb(255, 255, 255),
    .toolTipBase = qRgb(236, 235, 232), .toolTipText = qRgb(27, 26, 25),
    .light = qRgb(58, 57, 54), .midlight = qRgb(48, 47, 45),
    .mid = qRgb(20, 19, 18), .dark = qRgb(15, 14, 14), .shadow = qRgb(0, 0, 0),
    .link = qRgb(108, 176, 240), .linkVisited = qRgb(179, 157, 219),
    .disabledText = qRgb(122, 120, 116), .disabledHighlight = qRgb(58, 57, 54),
};

// --- spacing, on a 4px scale -------------------------------------------------
inline constexpr int kGapTight  = 8;
// The board's own inset. The window margin and the sidebar gap now frame it
// (kWindowMargin, kSplitterGap), so the board adds only what makes 24px to the
// sidebar and 16px to the window's edge, and starts level with the sidebar.
inline constexpr int kPadX      = 16;
inline constexpr int kPadTop    = 0;

// --- window and controls (the 2026-10-06 mockup) -----------------------------
inline constexpr int kWindowMargin  = 16;  // the desk's edge around everything
// The mockup measures 34 and 35; the nearest steps of the 4px scale.
inline constexpr int kHeaderTop     = 32;  // above the header row
inline constexpr int kHeaderGap     = 32;  // header row to sidebar and board
inline constexpr int kSplitterGap   = 8;   // sidebar to board, with kPadX after it
inline constexpr int kSidebarWidth  = 344; // the sidebar, and the search field above it
inline constexpr int kHeaderControlH = 38; // the header's search, switch and buttons
inline constexpr int kFieldHeight   = 34;  // a field or button in a dialog
inline constexpr int kControlPadX   = 14;
inline constexpr int kPillPadX      = 16;  // a pill's words from its rounded ends
inline constexpr int kControlRadius = 8;
inline constexpr int kMenuRadius    = 10;
inline constexpr int kGroupPad      = 14;
inline constexpr int kScrollBarExtent = 10;
// Hover and press are a step of the text colour over whatever is underneath,
// so they read the same on the window, a surface, or a card.
inline constexpr int kControlHoverAlpha = 16;
inline constexpr int kControlPressAlpha = 28;
// An unchecked box's outline carries meaning, so it meets 3:1 on the window and
// on a surface (3.75 and 3.94:1). It was 110, which measured 2.69:1.
inline constexpr int kControlEdgeAlpha  = 140;
inline constexpr int kFieldInset       = 12;   // a field's text from its edge
// A scroll thumb at rest: 80 measured 1.99:1 (independent review).
inline constexpr int kScrollThumbAlpha = 120;
inline constexpr int kMenuEdgeAlpha    = 70;   // a popup's edge, now without a platform shadow

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
inline constexpr int kCardPad      = 20;   // a text card's content inset
// An image card holds its picture nearly to the edge, as a photo in a frame;
// the footer below it keeps a little more room so its words do not crowd.
inline constexpr int kImagePad       = 6;
inline constexpr int kImageFooterInset = 6;   // added to kImagePad for the footer
inline constexpr int kImageGap        = 2;    // picture to footer
inline constexpr int kImageBottom     = 8;    // footer to the card's edge
inline constexpr int kCardFooterH  = 28;   // the copy action and the timestamp,
                                          // at the default font — use
                                          // footerHeight() for the real one
inline constexpr int kCardGap      = 24;

// The list's rows are cards too, but denser than the board's: a row is read in
// a glance down a column of them, a board card is read. So the row's inset is
// 14 against the board's 16 — a decision, not drift. These lived as unexplained
// constants in BufferCardDelegate.h.
inline constexpr int kRowMarginX  = 8;    // the row's distance from the sidebar's edges
inline constexpr int kRowMarginY  = 2;    // half the gap between two rows
inline constexpr int kRowPad      = 14;   // content inset within a row
inline constexpr int kRowSectionH = 36;   // a RECENT / OLDER label
// A row holding two words was 1360px wide on a wide window, with the state
// glyphs 1250px from the text they describe. Past this measure a row is mostly
// margin. Unrelated to kCardMaxWidth: that bounds a board column, this a row.
inline constexpr int kRowMaxWidth = 760;
inline constexpr int kRowThumb      = 52;   // a lone image
inline constexpr int kRowThumbMulti = 38;   // several in a row

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
// Since the mockup a resting card has no edge — its surface against the window
// is the edge — and these two border values remain for the places that still
// draw one. Under the pointer a card gains this quiet one.
inline constexpr int kCardHoverEdge = 70;

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
    // The two themes' own text colours, so text on a fill is never a third white.
    const QColor light = QColor::fromRgb(kDarkColors.text), dark = QColor::fromRgb(kLightColors.text);
    return contrastRatio(light, fill) >= contrastRatio(dark, fill) ? light : dark;
}

// Which side of the scale a colour is on. The one place this question is
// answered: it was written out inline four times.
inline bool isLight(const QColor& c) { return c.lightness() > 128; }

inline bool isLightTheme(const QPalette& pal) { return isLight(pal.color(QPalette::Window)); }

// Against both surfaces an accent edge can sit on: a selected card's edge is
// half on the card (Base) and half on the window around it, and a list row's
// rail is drawn on the chosen row's window-coloured fill. Measured against Base
// alone it reached 2.77:1 on the window (independent review, 2026-10-06).
inline QColor readableAccent(const QPalette& pal, qreal strength = 1.0)
{
    const QColor base = pal.color(QPalette::Base);
    const QColor window = pal.color(QPalette::Window);
    const bool light = isLight(base);
    QColor accent = pal.color(QPalette::Highlight);

    auto weakest = [&](const QColor& c) {
        return std::min(contrastRatio(c, base), contrastRatio(c, window));
    };
    for (int i = 0; i < 24 && weakest(accent) < 3.0; ++i)
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
    const int base = isLightTheme(pal) ? kCardBorderLight : kCardBorderDark;
    return hovered ? std::min(255, base + kCardBorderHoverBoost) : base;
}


// --- focus ---------------------------------------------------------------------
// Where the keyboard is, on a list row or a board card: a ring inset from the
// card's outer edge. The list wrote 3.5 and the board wrote 3 on a box already
// inset by 0.5 — the same ring, spelled two ways.
inline constexpr qreal kFocusInset = 3.5;
inline constexpr qreal kFocusWidth = 2.0;

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

// The footer holds text, so its height follows the font. Fixed at 28px it fit
// at the default size and clipped the card's own content at 200%: the board
// derives a card's chrome from this number, so a stale one is a card whose
// content area is smaller than the thing it was measured to hold.
inline int footerHeight(const QFont& font)
{
    return std::max(kCardFooterH, QFontMetrics(font).height() + 10);
}

// How far a card's content sits from its edge. One function, consulted by the
// card and by BoardLayout, which measures cards without building them: a card
// drawn one way and measured another gets a box of the wrong size.
inline int cardInset(bool image) { return image ? kImagePad : kCardPad; }

// Everything that is not the content: padding, the footer, and the gap to it.
inline int cardChromeHeight(const QFont& font, bool image = false)
{
    if (image) return kImagePad + kImageGap + footerHeight(font) + kImageBottom;
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

// No text is drawn smaller than this, whatever the scale or the text-size
// setting. The setting floored at 5pt while the scale floored at 7pt, so the
// same question had two answers.
inline constexpr qreal kMinPointSize = 7.0;

// The body size the design is drawn at: 14px at 96 dpi, as the mockup's cards.
// A desktop asking for larger wins (SettingsDialog::basePointSize).
inline constexpr qreal kBodyPointSize = 10.5;

inline QFont scaledBy(const QFont& base, qreal ratio, int weight = -1)
{
    QFont f = base;
    f.setPointSizeF(std::max(kMinPointSize, base.pointSizeF() * ratio));
    if (weight >= 0) f.setWeight(QFont::Weight(weight));
    return f;
}

}  // namespace napkin::tokens
