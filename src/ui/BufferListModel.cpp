#include "BufferListModel.h"
#include "../data/BufferRepository.h"
#include "../data/ItemRepository.h"
#include "../domain/BufferService.h"
#include "../domain/Clock.h"
#include "../domain/Search.h"
#include "../domain/TimeFormat.h"
#include "../data/Database.h"

namespace napkin {
namespace {
// Generous enough that scrolling never blocks on a query, small enough that we
// are never reading the whole table. Revisited if §12 measurements say so.
constexpr int kMaxRows = 5000;
constexpr int kPreviewCacheLimit = 400;
}  // namespace

BufferListModel::BufferListModel(Database& db, BufferRepository& buffers,
                                 ItemRepository& items, QObject* parent)
    : QAbstractListModel(parent), db_(db), buffers_(buffers), items_(items)
{
    reload();
}

int BufferListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(rows_.size());
}

void BufferListModel::setQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (query_ == trimmed) return;
    const QString previous = query_;
    query_ = trimmed;
    frozen_ = false;      // a search is a deliberate reorder; nothing is being typed into
    try {
        reload();
    } catch (...) {
        // The rows still belong to the previous query, so the query must too:
        // highlighting and the empty state both read it.
        query_ = previous;
        throw;
    }
}

void BufferListModel::setMode(Mode mode)
{
    if (mode_ == mode) return;
    const Mode previous = mode_;
    mode_ = mode;
    frozen_ = false;   // nothing is being edited across a mode switch
    try {
        reload();
    } catch (...) {
        mode_ = previous;   // as setQuery: the rows and the mode stay a pair
        throw;
    }
}

void BufferListModel::reload()
{
    if (frozen_) { pendingReload_ = true; return; }  // see freezeOrder

    // Query first, reset second. A query can throw, and throwing between
    // beginResetModel() and endResetModel() leaves every attached view waiting
    // for a reset that never ends, over rows that were already cleared.
    std::vector<Buffer> rows;
    QHash<BufferId, QString> snippets;
    if (isSearching()) {
        // Ranked by relevance, so the order deliberately differs from the
        // ordinary recency order.
        for (const auto& hit : searchBuffers(db_, query_, kMaxRows)) {
            if (const auto buffer = buffers_.find(hit.bufferId)) {
                rows.push_back(*buffer);
                snippets.insert(hit.bufferId, hit.snippet);
            }
        }
    } else {
        rows = mode_ == Mode::Live ? buffers_.listLive(kMaxRows) : buffers_.listTrash();
    }

    beginResetModel();
    rows_ = std::move(rows);
    snippets_ = std::move(snippets);
    previewCache_.clear();
    endResetModel();
    emit countChanged(int(rows_.size()));
}

BufferId BufferListModel::idAt(int row) const
{
    if (row < 0 || row >= int(rows_.size())) return kNoBuffer;
    return rows_[size_t(row)].id;
}

int BufferListModel::rowForId(BufferId id) const
{
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) return int(i);
    return -1;
}

int BufferListModel::draftRow() const
{
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == kNoBuffer) return int(i);
    return -1;
}

BufferPreview BufferListModel::previewFor(BufferId id) const
{
    QString name;
    for (const auto& b : rows_)
        if (b.id == id) { name = b.name; break; }
    if (const auto it = previewCache_.constFind(id); it != previewCache_.constEnd())
        return *it;

    // The cache was previously unbounded and only ever cleared on a full
    // reload, so scrolling a large list accumulated every buffer's preview text
    // for the lifetime of the window. A screenful is ~12 rows; this is
    // generous, and dropping it whole is cheap because a miss is two indexed
    // queries.
    if (previewCache_.size() >= kPreviewCacheLimit) previewCache_.clear();
    const auto counts = items_.countsForBuffer(id);
    // Two, so the newest can be skipped when it is the item the title came from.
    const auto preview = derivePreview(items_.previewHead(id), counts.total, counts.images,
                                       counts.total > 1 ? items_.latestItems(id, 2)
                                                        : std::vector<Item>{},
                                       name);
    previewCache_.insert(id, preview);
    return preview;
}

QVariant BufferListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(rows_.size())) return {};
    const int row = index.row();
    const Buffer& b = rows_[size_t(row)];
    const bool isDraft = b.id == kNoBuffer;

    // Metadata roles first, and WITHOUT touching the preview. sizeHint() reads
    // IsExpandedRole for every row in the list, and QListView asks every row for
    // its size hint — so computing a preview up here turned a 5000-buffer
    // startup into 10 001 SQL statements and materialised every buffer's text.
    switch (role) {
    case IdRole:         return QVariant::fromValue(b.id);
    case ModifiedAtRole: return QVariant::fromValue(b.modifiedAt);
    case PinnedRole:     return b.pinned;
    case KeptRole:       return b.kept;
    case IsDraftRole:    return isDraft;
    case SectionFirstRole: {
        if (isSearching()) return row == 0;
        if (mode_ == Mode::Trash) return row == 0;
        if (row == 0) return true;
        const Buffer& above = rows_[size_t(row) - 1];
        if (above.pinned != b.pinned) return true;
        // RECENT gives way to OLDER at the cutoff. Age changes where a buffer
        // sits, never whether it exists (SPEC.md §6).
        return !b.pinned && isOlder(above) != isOlder(b);
    }
    case SectionNameRole: {
        if (isSearching()) {
            // Spelled out rather than tr("%n RESULT(S)"): without a loaded
            // translation Qt uses the source string verbatim, so the header
            // read "3 RESULT(S)".
            const int n = int(rows_.size());
            return n == 1 ? tr("1 RESULT") : tr("%1 RESULTS").arg(n);
        }
        if (mode_ == Mode::Trash) return QStringLiteral("TRASH");
        if (b.pinned) return QStringLiteral("PINNED");
        return isOlder(b) ? QStringLiteral("OLDER") : QStringLiteral("RECENT");
    }
    case IsOlderRole: return isOlder(b);
    case Qt::ToolTipRole: {
        // The pin and keep glyphs are small and similar in weight; hovering a
        // row says in words which it carries and what that means.
        QStringList says;
        if (b.pinned) says << tr("Pinned — stays at the top of the list");
        if (b.kept)   says << tr("Kept — Clean up never moves it to the trash");
        if (says.isEmpty()) return {};
        return says.join(QLatin1Char('\n'));
    }
    default:
        break;
    }

    const BufferPreview p = isDraft ? draftPreview_ : previewFor(b.id);

    switch (role) {
    case PrimaryRole:    return p.primary;
    case SecondaryRole:  return p.secondary;
    case ItemCountRole:  return p.itemCount;
    case HasImageRole:   return p.hasImage();
    case ImageCountRole: return p.imageCount;
    case ThumbHashRole:  return p.thumbs.empty() ? QString() : p.thumbs.front().hash;
    case ThumbMimeRole:  return p.thumbs.empty() ? QString() : p.thumbs.front().mime;
    case ThumbAnimatedRole: {
        for (const auto& t : p.thumbs) if (t.animated) return true;
        return false;
    }
    case ThumbCountRole: return int(p.thumbs.size());
    case SnippetRole:    return snippets_.value(b.id);
    case LatestRole:     return p.latest;
    case Qt::AccessibleTextRole: {
        // Never encode state in styling alone (SPEC.md §14).
        QString label = p.primary.isEmpty() ? tr("Empty napkin") : p.primary;
        if (!p.secondary.isEmpty()) label += QStringLiteral(". ") + p.secondary;
        if (b.pinned) label += tr(". Pinned");
        if (b.kept)   label += tr(". Kept");
        if (b.inTrash()) label += tr(". In trash");
        // A sighted user reads "2 minutes ago" off the card; without this a
        // screen-reader user gets no recency at all, on a recency-ordered list.
        if (!isDraft) label += QStringLiteral(". ") + relativeTime(b.modifiedAt, nowMs());
        return label;
    }
    default: return {};
    }
}

Qt::ItemFlags BufferListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void BufferListModel::freezeOrder(bool frozen)
{
    if (frozen_ == frozen) return;
    frozen_ = frozen;

    // The list re-sorts only once nothing is being edited.
    if (!frozen_ && pendingReload_) {
        pendingReload_ = false;
        reload();
    }
}

int BufferListModel::insertDraftRow()
{
    if (const int existing = draftRow(); existing >= 0) return existing;

    // A new draft is the most recent thing there is, so it goes at the top of
    // RECENT — below pinned buffers, above everything else.
    int row = 0;
    while (row < int(rows_.size()) && rows_[size_t(row)].pinned) ++row;

    Buffer draft;
    draft.id = kNoBuffer;
    draft.createdAt = draft.modifiedAt = nowMs();

    beginInsertRows({}, row, row);
    rows_.insert(rows_.begin() + row, draft);
    draftPreview_ = {};
    endInsertRows();
    return row;
}

void BufferListModel::setDraftPreview(const BufferPreview& preview)
{
    const int row = draftRow();
    if (row < 0) return;
    draftPreview_ = preview;
    emit dataChanged(index(row), index(row));
}

void BufferListModel::promoteDraft(BufferId newId)
{
    const int row = draftRow();
    if (row < 0) return;
    rows_[size_t(row)].id = newId;
    previewCache_.insert(newId, draftPreview_);
    emit dataChanged(index(row), index(row));
    emit countChanged(int(rows_.size()));
}

void BufferListModel::removeDraftRow()
{
    const int row = draftRow();
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
    emit countChanged(int(rows_.size()));
}

bool BufferListModel::isOlder(const Buffer& buffer) const
{
    return !buffer.pinned && buffer.modifiedAt < BufferService::olderThanCutoff();
}

int BufferListModel::sweepableCount() const
{
    // Deliberately NOT isOlder(), which answers a different question. isOlder is
    // about placement and so excludes pinned buffers, because a pinned buffer is
    // not in the recency order at all. Sweep eligibility is about lifecycle:
    // only Keep protects. Pinning does not, and conflating the two would make
    // pinning a silent second Keep — exactly the confusion §3 exists to prevent.
    const Timestamp cutoff = BufferService::olderThanCutoff();
    int n = 0;
    for (const auto& b : rows_)
        if (!b.kept && b.modifiedAt < cutoff) ++n;
    return n;
}

std::vector<ImageRef> BufferListModel::thumbsAt(int row) const
{
    if (row < 0 || row >= int(rows_.size())) return {};
    const Buffer& b = rows_[size_t(row)];
    return b.id == kNoBuffer ? draftPreview_.thumbs : previewFor(b.id).thumbs;
}

void BufferListModel::refreshRow(BufferId id)
{
    const int row = rowForId(id);
    if (row < 0) return;
    if (const auto fresh = buffers_.find(id)) rows_[size_t(row)] = *fresh;
    emit dataChanged(index(row), index(row));
}

void BufferListModel::invalidatePreview(BufferId id)
{
    previewCache_.remove(id);
    const int row = rowForId(id);
    if (row < 0) return;
    // Re-read the row itself, not only its preview. While a napkin is being
    // worked on the list does not re-sort (freezeOrder), so its row kept the
    // modified time from the last reload: edit a note and the list still said
    // "23 minutes ago". The time updates here; the position waits, as before.
    if (id != kNoBuffer)
        if (const auto fresh = buffers_.find(id)) rows_[size_t(row)] = *fresh;
    emit dataChanged(index(row), index(row));
}

void BufferListModel::refreshTimestamps()
{
    emitAllChanged();
}

void BufferListModel::emitAllChanged()
{
    if (rows_.empty()) return;
    emit dataChanged(index(0), index(int(rows_.size()) - 1), {ModifiedAtRole});
}

}  // namespace napkin
