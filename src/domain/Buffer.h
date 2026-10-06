#pragma once
#include "Types.h"
#include <QString>
#include <optional>

namespace napkin {

// Metadata only. Items are loaded separately so the list view never pays for
// content it does not draw (SPEC.md §12: windowed queries, never SELECT *).
struct Buffer {
    BufferId  id         = kNoBuffer;
    Timestamp createdAt  = 0;
    Timestamp modifiedAt = 0;
    bool      pinned     = false;
    bool      kept       = false;
    std::optional<Timestamp> deletedAt;  // set => in trash
    // Chosen by the user, and never required. Empty means the napkin is titled
    // from its contents, as it always was (§3).
    QString   name;

    bool isPersisted() const { return id != kNoBuffer; }
    bool inTrash() const { return deletedAt.has_value(); }
};

}  // namespace napkin
