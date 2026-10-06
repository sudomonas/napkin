#pragma once
#include "Item.h"
#include <QString>
#include <vector>

namespace napkin {

// Derived display, never user-entered metadata (SPEC.md §1, §3). Napkin has no
// title field; a card's label is computed from what the buffer already holds.
// One. Three of them squeezed the only identifying string in the row down to
// "This is some of the mult…", and the canvas beside it now shows every image
// anyway — the list only has to say "there are pictures in here".
inline constexpr int kMaxCardThumbs = 1;

struct ImageRef {
    QString hash;
    QString mime;
    bool    animated = false;
};

struct BufferPreview {
    QString primary;     // the line the card leads with
    QString secondary;   // quiet detail line; may be empty
    // The most recent addition, when it is not the item the title came from.
    // The title is deliberately the FIRST thing put down (§7), so without this
    // a list row never showed what was last added — which is what a test user
    // looked for first (2026-10-06).
    QString latest;
    int     itemCount  = 0;
    int     imageCount = 0;
    std::vector<ImageRef> thumbs;   // first kMaxCardThumbs images, in order

    bool hasImage() const { return imageCount > 0; }
    bool isEmpty() const { return primary.isEmpty() && secondary.isEmpty() && thumbs.empty(); }
};

// `head` is the first few items of the buffer in position order; `totalCount`
// is how many it actually has. Only the head is loaded so a list of 5000
// buffers never reads every item (§12).
// `latest` is the top of the board, newest first; may be empty.
// `name` is the user's own, if they gave one: it is the title, and the latest
// line then never skips the item the title would otherwise have come from.
BufferPreview derivePreview(const std::vector<Item>& head, int totalCount, int imageCount,
                            const std::vector<Item>& latest = {}, const QString& name = {});

// How many items the preview needs: enough to find an image a line or two down.
inline constexpr int kPreviewHeadSize = 8;

// A card draws at most a line or two. Keeping the whole of a pasted minified
// bundle in the preview made elidedText O(text length): a single 50 000-char
// line cost 7 ms per paint, and a million-char line 153 ms — for one card, on
// every repaint, including the once-a-minute timestamp tick.
inline constexpr int kPreviewLineLimit = 256;

// First non-blank line of a block of text, whitespace-trimmed.
QString firstLine(const QString& text);

// Human-readable byte size: "4.2 MB".
QString formatBytes(qint64 bytes);

}  // namespace napkin
