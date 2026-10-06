#include "Search.h"
#include "../data/Database.h"
#include "../data/Statement.h"

#include <QStringList>

namespace napkin {

QString toMatchExpression(const QString& typed)
{
    const QStringList words = typed.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) return {};

    QStringList terms;
    terms.reserve(words.size());
    for (int i = 0; i < words.size(); ++i) {
        // Quote: FTS5 reads bare AND / OR / NOT / NEAR as operators, and a
        // lone quote or apostrophe is a syntax error rather than a search.
        QString word = words.at(i);
        word.replace(QLatin1Char('"'), QLatin1String("\"\""));
        // Prefix-match the final word so results narrow while you are still
        // typing it. Earlier words are complete by definition.
        const bool last = i == words.size() - 1;
        terms << (last ? QStringLiteral("\"%1\"*").arg(word)
                       : QStringLiteral("\"%1\"").arg(word));
    }
    return terms.join(QLatin1Char(' '));
}

std::vector<SearchHit> searchBuffers(Database& db, const QString& typed, int limit, bool inTrash)
{
    const QString match = toMatchExpression(typed);
    if (match.isEmpty()) return {};

    // Rolled up to the buffer, ranked by the best-scoring item in it, with the
    // snippet taken from that same item. Trashed buffers are excluded: search
    // is for finding what you have, not what you threw away.
    // The LIMIT -1 is load-bearing, not decoration. FTS5's auxiliary functions
    // (bm25, snippet) only work when the FTS table is the direct subject of the
    // query — and SQLite flattens an ordinary CTE into the outer join, which
    // puts them back in a context they refuse with "unable to use function bm25
    // in the requested context". A subquery with a LIMIT is never flattened
    // into a join, so the ranking and the snippet stay in a query where the
    // index is all there is.
    //
    // This used to say AS MATERIALIZED, which works only from SQLite 3.39: on
    // 3.35–3.38 the hint did not stop the flattening, and before 3.35 it is a
    // syntax error. Ubuntu 22.04 ships 3.37.2 and the AppImage is built there,
    // so every search with a hit failed in the v0.1.0 release build. CI's
    // old-SQLite job exists because of this.
    //
    // MIN(h.rank) with a bare h.snip beside it is the documented SQLite
    // behaviour of returning the snippet from the row that produced the
    // minimum: the snippet shown is the best-matching item's, not an arbitrary
    // one.
    const QString where = QString::fromLatin1(inTrash ? "NOT NULL" : "NULL");
    // fromUtf8: the snippet's ellipsis is not Latin-1.
    const QByteArray sql = QString::fromUtf8(
        "WITH hits AS ("
        "  SELECT rowid AS item_id,"
        "         bm25(items_fts) AS rank,"
        "         snippet(items_fts, 0, char(2), char(3), '…', 12) AS snip"
        "    FROM items_fts WHERE items_fts MATCH ? LIMIT -1)"
        "SELECT b.id, COUNT(*), h.snip, MIN(h.rank)"
        "  FROM hits h"
        "  JOIN items     ON items.id = h.item_id"
        "  JOIN buffers b ON b.id = items.buffer_id"
        " WHERE b.deleted_at IS %1"
        " GROUP BY b.id"
        " ORDER BY MIN(h.rank) ASC, b.modified_at DESC"
        " LIMIT ?").arg(where).toUtf8();
    Statement s(db, sql.constData());
    s.bind(1, match).bind(2, limit);

    std::vector<SearchHit> hits;
    while (s.step()) {
        SearchHit hit;
        hit.bufferId = s.columnInt64(0);
        hit.matchingItems = s.columnInt(1);
        hit.snippet = s.columnText(2).simplified();
        hit.rank = s.columnDouble(3);
        hits.push_back(hit);
    }

    // A name the user gave is the first thing they will search for, and it is
    // not in the item index. Few napkins have one, so they are simply read and
    // compared: every word typed must appear in the name. A napkin found by
    // name goes first — the user said what it is called.
    const QStringList words = typed.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    Statement named(db, inTrash ? "SELECT id, name FROM buffers"
                                  " WHERE deleted_at IS NOT NULL AND name IS NOT NULL"
                                : "SELECT id, name FROM buffers"
                                  " WHERE deleted_at IS NULL AND name IS NOT NULL");
    std::vector<SearchHit> byName;
    while (named.step()) {
        const QString name = named.columnText(1);
        bool all = true;
        for (const auto& w : words) all &= name.contains(w, Qt::CaseInsensitive);
        if (!all) continue;
        SearchHit hit;
        hit.bufferId = named.columnInt64(0);
        hit.rank = -1e9;
        for (auto it = hits.begin(); it != hits.end(); ++it)
            if (it->bufferId == hit.bufferId) { hit = *it; hit.rank = -1e9; hits.erase(it); break; }
        byName.push_back(hit);
    }
    hits.insert(hits.begin(), byName.begin(), byName.end());
    if (int(hits.size()) > limit) hits.resize(size_t(limit));
    return hits;
}

std::vector<ItemId> matchingItems(Database& db, BufferId buffer, const QString& typed)
{
    const QString match = toMatchExpression(typed);
    if (match.isEmpty()) return {};

    Statement s(db,
        "SELECT items.id FROM items_fts"
        "  JOIN items ON items.id = items_fts.rowid"
        " WHERE items_fts MATCH ? AND items.buffer_id = ?");
    s.bind(1, match).bind(2, buffer);

    std::vector<ItemId> out;
    while (s.step()) out.push_back(s.columnInt64(0));
    return out;
}

}  // namespace napkin
