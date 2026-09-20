#pragma once
#include "../domain/Types.h"
#include "../media/Exporter.h"   // Result is a nested type, so it cannot be forward-declared
#include <QList>
#include <QSet>
#include <QString>
#include <functional>
#include <QMainWindow>

class QAction;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QStackedWidget;
class QMenu;
class QSplitter;
class QTimer;

namespace napkin {

class Autosave;
class UndoToast;
class BufferListModel;
class BufferListView;
class ItemCanvas;
class BufferRepository;
class BufferService;
class EmptyStateView;
class TrayIcon;
class WelcomeView;
class BlobStore;
class Database;
class ItemRepository;
class Thumbnailer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Database& db, BufferRepository& buffers, ItemRepository& items,
               BufferService& service, BlobStore& blobs, Thumbnailer& thumbs,
               QWidget* parent = nullptr);

public slots:
    void raiseFromOtherInstance();

    // Public and named so it can be reached from a menu, a toolbar or a test,
    // rather than only through a key chord.
    void newDraft();
    void togglePin(int row);
    void toggleKeep(int row);
    void trashRow(int row);
    void showTrash(bool trash);
    void reviewSweep();
    void restoreRow(int row);
    void showShortcuts();
    void emptyTrashForTest();
    QSet<QString> undoProtectedBlobsForTest() const { return undoProtectedBlobs_; }
    void updateSweepNudgeForTest() { updateSweepNudge(); }
    void sweepForTest(const QList<BufferId>& ids);
    // The undo path normally runs from the toast; tests drive it directly.
    void undoLastTrashForTest(BufferId id, bool wasKept, Timestamp modifiedAt);
    void emptyTrash();
    // "Quit" has to end the application from wherever it is asked for. It
    // cannot be spelled close(); see the definition. Returns whether Napkin is
    // actually going — unsaved text that cannot be written refuses the quit,
    // and then `quitting` is not emitted.
    bool quitNapkin();

signals:
    // Napkin is going. It exists so that "Quit actually quits" is observable:
    // the quit itself is a call into the application object, which a test
    // running outside exec() cannot see happen.
    void quitting();

public:
    void pasteFromClipboard();
    // Paste onto a new napkin, from the tray. Separate from pasteFromClipboard
    // because the clipboard cannot be read until the window has focus.
    void pasteOntoNewNapkinFromTray();
    void addImageFromFile();
    void openImageItem(ItemId id);
    void openRow(int row);
    void selectBuffer(int row);
    // What Delete and Cut do: the items go to the trash (see
    // BufferService::trashItems), with Undo offered on the toast.
    void removeItems(const QList<ItemId>& ids, bool cut = false);
    // A text card the user emptied: there is nothing left to recover, so the
    // item goes for good rather than into the trash as a blank napkin.
    void discardItems(const QList<ItemId>& ids);
    // Returns whether text was actually stored.
    bool appendTextBlock(const QString& text = {});

protected:
    void closeEvent(QCloseEvent* e) override;
    bool event(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* e) override;

private:
    void updateEmptyTrashButton();
    void styleMenuBar();
    // The toolbar's drawn icons carry the theme's colour, so they are redrawn
    // whenever the palette changes.
    void styleToolbarIcons();
    void buildUi();
    QWidget* buildHeaderWidget();
    QMenu* buildOverflowMenu();
    void buildMenuBar();
    void openSettings();
    void goHome();
    void applyTraySetting();
    // Runs `then` once the window has actually taken focus, or gives up after
    // ~1s. Anything that reads the clipboard after a raise must go through
    // this; see pasteOntoNewNapkinFromTray().
    void whenWindowIsActive(std::function<void()> then);
    void exportCurrentBuffer();
    void exportEverything();
    // Both export paths end here, so the result is reported the same way and
    // the problem list can never be dropped on one of them.
    void reportExport(const Exporter::Result& result, const QString& what);
    // Returns false when the write failed. The caller must NOT collapse or close
    // on a false: doing so strands the text in a widget that is about to go
    // away, and the next flush returns early because nothing is being edited.
    bool flushEditor();
    bool flushAndReportFailure();
    void updateEmptyState();
    void updateSweepNudge();
    void showContextMenu(int row, const QPoint& globalPos);
    void reloadPreservingSelection();
    bool addImageToCurrent(const QByteArray& bytes, const QString& mime, const QString& sourceName);
    void reportProblem(const QString& title, const QString& detail);

    // Runs work that touches the database and turns a failure into a message
    // rather than a crash. An exception thrown inside a slot unwinds into Qt's
    // event loop, which calls std::terminate — so nothing that can throw may
    // reach it uncaught.
    bool guarded(const QString& title, const std::function<void()>& work);

    // editingBuffer_ names a row that may have been trashed or purged since it
    // was selected. Anything that writes to it must check first.
    bool currentBufferIsLive();

    Database&         db_;
    BufferRepository& buffers_;
    ItemRepository&   items_;
    BufferService&    service_;
    BlobStore&        blobs_;
    Thumbnailer&      thumbs_;

    BufferListModel* model_  = nullptr;
    BufferListView*  view_   = nullptr;
    ItemCanvas*      canvas_ = nullptr;
    QSplitter*       splitter_ = nullptr;
    QStackedWidget*  stack_  = nullptr;
    Autosave*        autosave_ = nullptr;
    UndoToast*       toast_ = nullptr;
    // What a trashed buffer looked like before it was trashed, so Undo can put
    // it back as it was rather than as a stripped copy of itself.
    struct TrashedState { BufferId id = kNoBuffer; bool kept = false; Timestamp modifiedAt = 0; };
    TrashedState lastTrashed_;
    // Blobs whose rows are gone but which the live undo offer would restore.
    QSet<QString> undoProtectedBlobs_;

    // A cut waits in the trash until it is pasted; then the trashed original
    // is discarded, so a completed move leaves nothing behind (second usability
    // test: "an Image in the trash I never deleted"). Only when the clipboard
    // still holds exactly what the cut put there, and only when that copy is
    // the whole of what was cut — a mixed selection copies as text alone, and
    // discarding its originals would lose the images.
    struct PendingCut {
        const void* clip = nullptr;   // identity of the clipboard's data at the cut
        QString     text;             // and its text, against address reuse
        BufferId    holder = kNoBuffer;
    } pendingCut_;
    void completePendingCut();
    int saveFailures_ = 0;
    QTimer*          timeRefresh_ = nullptr;
    QAction*         trashAction_ = nullptr;
    QPushButton*     emptyTrashButton_ = nullptr;
    QToolButton*     overflowButton_ = nullptr;
    QPushButton*     trashToggle_ = nullptr;
    QPushButton*     newButton_ = nullptr;
    QToolButton*     settingsButton_ = nullptr;
    QAction*         showTrashAction_ = nullptr;
    QAction*         exportBufferAction_ = nullptr;
    QLineEdit*       search_ = nullptr;
    QTimer*          searchDebounce_ = nullptr;
    QWidget*         filterBanner_ = nullptr;
    QLabel*          filterLabel_ = nullptr;
    QWidget*         sweepNudge_ = nullptr;
    QLabel*          sweepLabel_ = nullptr;
    bool             nudgeDismissed_ = false;
    WelcomeView*     welcome_ = nullptr;
    EmptyStateView*  emptyState_ = nullptr;
    TrayIcon*        tray_ = nullptr;
    bool             reallyQuitting_ = false;

    // What the open editor is bound to. kNoBuffer means an uncommitted draft,
    // which by invariant 5 has no row in the database yet.
    BufferId editingBuffer_ = kNoBuffer;
    ItemId   editingItem_   = kNoItem;
};

}  // namespace napkin
