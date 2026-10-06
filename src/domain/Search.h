#pragma once
#include "Types.h"
#include <QString>
#include <vector>

namespace napkin {

class Database;

// One buffer that matched, with enough context to show why.
struct SearchHit {
    BufferId bufferId = kNoBuffer;
    int      matchingItems = 0;
    QString  snippet;      // from the best-matching item, with the term marked
    qreal    rank = 0;     // bm25; lower is better
};

// Turns what a person typed into something FTS5 will accept.
//
// FTS5's query language is not free text: a bare apostrophe, a stray quote or a
// word like AND or NOT changes the meaning or raises a syntax error. Every token
// is therefore quoted, and the last one gets a prefix wildcard so results narrow
// as you type rather than appearing only on a complete word.
QString toMatchExpression(const QString& typed);

// Buffers whose items match, best first. Rolled up to buffer level because that
// is the unit the list shows — a hit on item 3's filename surfaces the whole
// buffer (SPEC.md §5).
// `inTrash` searches the trash instead of the live napkins: one or the other,
// as the list shows one or the other (the Trash tab searched the live napkins
// and found nothing that was in it — usability test, 2026-10-06).
std::vector<SearchHit> searchBuffers(Database& db, const QString& typed, int limit,
                                     bool inTrash = false);

// Items within one buffer that match, so the board can mark them.
std::vector<ItemId> matchingItems(Database& db, BufferId buffer, const QString& typed);

}  // namespace napkin
