#pragma once
#include "../src/data/BufferRepository.h"
#include "../src/data/Database.h"
#include "../src/data/ItemRepository.h"
#include "../src/domain/BufferService.h"
#include "../src/media/BlobStore.h"
#include "../src/media/Thumbnailer.h"
#include "../src/ui/BufferListModel.h"
#include "../src/ui/BufferListView.h"
#include "../src/ui/ItemCanvas.h"
#include "../src/ui/ItemCard.h"
#include "../src/ui/MainWindow.h"
#include "../src/ui/UndoToast.h"

#include <QAction>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QtTest>

// Shared by the GUI suites. The database must be open before MainWindow is
// constructed — its model queries on construction — and a base class is
// initialized before members, so the ordering is guaranteed rather than merely
// observed. Blobs go to a temporary directory; no test touches real user data.
struct GuiFixture {
private:
    struct Base {
        QTemporaryDir dir;
        napkin::Database db;
        // On disk only when a test needs a second connection to see the same
        // database — the background saver's does.
        explicit Base(bool onDisk)
        {
            db.open(onDisk ? dir.path() + QStringLiteral("/napkin.db") : QStringLiteral(":memory:"));
        }
    };
    Base base_{false};

public:
    napkin::Database& db = base_.db;
    napkin::BufferRepository buffers{db};
    napkin::ItemRepository items{db};
    napkin::BufferService service{db, buffers, items};
    napkin::BlobStore blobs{base_.dir.path() + "/blobs"};
    napkin::Thumbnailer thumbs{base_.dir.path() + "/thumbs", blobs};
    napkin::MainWindow window{db, buffers, items, service, blobs, thumbs};

    GuiFixture() { window.show(); }
    struct OnDisk {};
    explicit GuiFixture(OnDisk) : base_(true) { window.show(); }

    napkin::BufferListModel* model() { return window.findChild<napkin::BufferListModel*>(); }
    napkin::BufferListView*  view()  { return window.findChild<napkin::BufferListView*>(); }
    napkin::UndoToast*       toast() { return window.findChild<napkin::UndoToast*>(); }
    napkin::ItemCanvas*      canvas() { return window.findChild<napkin::ItemCanvas*>(); }

    // Selecting a row in the list is what shows a buffer now; there is no
    // expand step.
    void select(napkin::BufferId id)
    {
        const int row = model()->rowForId(id);
        QVERIFY(row >= 0);
        view()->setCurrentIndex(model()->index(row, 0));
    }
    // Cards are newest-first, so the first text card is the newest one — which
    // is also the pending card Ctrl+T just created.
    QPlainTextEdit* editor()
    {
        const auto cards = window.findChildren<napkin::TextItemCard*>();
        if (cards.isEmpty()) return nullptr;
        // An unwritten card is the one waiting to be typed into.
        for (auto* card : cards)
            if (card->isComposer()) return card->findChild<QPlainTextEdit*>();
        return cards.first()->findChild<QPlainTextEdit*>();
    }

    // Ctrl+N gives an empty buffer waiting to be pasted into; Ctrl+T is how you
    // ask for somewhere to type.
    QPlainTextEdit* newTextCard()
    {
        canvas()->addPendingTextCard();
        return editor();
    }

    QString thumbsDir() const { return base_.dir.path() + "/thumbs"; }

    // Counts statements SQLite has prepared, for asserting that a code path
    // does no querying at all.
    int statementCount() const
    {
        int count = 0;
        for (sqlite3_stmt* s = sqlite3_next_stmt(db.handle(), nullptr); s;
             s = sqlite3_next_stmt(db.handle(), s))
            count += sqlite3_stmt_status(s, SQLITE_STMTSTATUS_RUN, 0);
        return count;
    }

    napkin::BufferId seed(const char* text)
    {
        const auto id = buffers.create();
        service.appendTo(id, napkin::Item::makeText(QString::fromUtf8(text)));
        model()->reload();
        return id;
    }

    // Triggers the action a shortcut is bound to. Headless platforms never make
    // a window active, so key-chord delivery cannot be relied on here; the
    // action is the unit under test either way.
    void trigger(const char* actionName)
    {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(actionName));
        QVERIFY(action);
        action->trigger();
    }
};
