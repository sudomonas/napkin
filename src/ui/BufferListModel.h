#pragma once
#include "../domain/Buffer.h"
#include "../domain/Preview.h"
#include <QAbstractListModel>
#include <QHash>
#include <vector>

namespace napkin {

class BufferRepository;
class Database;
class ItemRepository;

// Holds buffer *metadata* for every live buffer — roughly 48 bytes a row, so
// 5000 buffers is a quarter of a megabyte — and fetches previews lazily, per
// visible row, into a cache. That is what keeps §12's promise without loading
// every item in the database to draw a list.
class BufferListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        PrimaryRole,
        SecondaryRole,
        ItemCountRole,
        HasImageRole,
        ThumbHashRole,
        ThumbMimeRole,
        ThumbAnimatedRole,
        ThumbCountRole,
        ImageCountRole,
        ModifiedAtRole,
        PinnedRole,
        KeptRole,
        IsDraftRole,
        SectionFirstRole,   // this row starts a section
        SectionNameRole,    // "PINNED" / "RECENT" / "TRASH" / "RESULTS"
        SnippetRole,        // why this buffer matched, when searching
        LatestRole,         // the most recent addition, when it is not the title
        IsOlderRole,        // past the age cutoff, so drawn quieter
        OriginRole,         // in the trash: the napkin these items came from, by title
        DeletedAtRole,      // in the trash: when it went there (0 when it is not)
        HeadRole,           // the title its content gives, even where PrimaryRole says something else
    };

    // Live shows the stack; Trash shows what is recoverable. Same rows, same
    // delegate — only the query and the available actions differ.
    enum class Mode { Live, Trash };

    BufferListModel(Database& db, BufferRepository& buffers, ItemRepository& items,
                    QObject* parent = nullptr);

    Mode mode() const { return mode_; }
    void setMode(Mode mode);

    // An empty query returns the list to whatever it was showing.
    void setQuery(const QString& query);
    QString query() const { return query_; }
    bool isSearching() const { return !query_.isEmpty(); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void reload();

    BufferId idAt(int row) const;
    int rowForId(BufferId id) const;

    // --- order freeze ---------------------------------------------------------
    // Reloading is deferred while the order is frozen. Autosave bumps
    // modified_at on every flush, and re-sorting on that would make the card
    // whose contents you are typing into jump to the top of the list while you
    // type (SPEC.md §7). The canvas freezes on the first edit and releases when
    // the selection moves on.
    bool orderFrozen() const { return frozen_; }
    void freezeOrder(bool frozen);

    // --- draft --------------------------------------------------------------
    // A draft row exists only in the model until it has content (invariant 5).
    int  insertDraftRow();
    int  draftRow() const;
    bool hasDraft() const { return draftRow() >= 0; }
    void setDraftPreview(const BufferPreview& preview);
    void promoteDraft(BufferId newId);
    void removeDraftRow();

    void invalidatePreview(BufferId id);

    // Re-reads a single row's metadata without resetting the model, so toggling
    // a flag does not cost the selection or the scroll position.
    void refreshRow(BufferId id);
    void refreshTimestamps();

    // Past the age cutoff AND in the recency order — so pinned buffers are
    // never "older", because they are not sorted by recency at all. This is a
    // question about PLACEMENT.
    bool isOlder(const Buffer& buffer) const;

    // How many a sweep would offer. A question about LIFECYCLE, so only Keep
    // excludes a buffer; pinning does not.
    int  sweepableCount() const;
    int previewCacheSize() const { return int(previewCache_.size()); }

signals:
    void countChanged(int liveCount);

private:
    BufferPreview previewFor(BufferId id) const;

public:
    // The delegate needs the whole thumbnail row, which does not fit a QVariant
    // role cleanly.
    std::vector<ImageRef> thumbsAt(int row) const;

private:
    void emitAllChanged();

    Database&         db_;
    BufferRepository& buffers_;
    ItemRepository&   items_;

    std::vector<Buffer> rows_;
    mutable QHash<BufferId, BufferPreview> previewCache_;
    BufferPreview draftPreview_;
    Mode mode_ = Mode::Live;
    QString query_;
    QHash<BufferId, QString> snippets_;
    // Trash rows holding items deleted from another napkin: that napkin's title,
    // read once per reload (it is not in the list, so not in previewCache_).
    QHash<BufferId, QString> origins_;

    bool frozen_        = false;
    bool pendingReload_ = false;
};

}  // namespace napkin
