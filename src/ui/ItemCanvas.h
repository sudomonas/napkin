#pragma once
#include "../domain/Item.h"
#include "BoardLayout.h"
#include <QScrollArea>

class QKeyEvent;
class QWheelEvent;
class QContextMenuEvent;

#include <QHash>
#include <QSet>
#include <vector>

class QLabel;
class QTimer;
class QPropertyAnimation;


namespace napkin {

class BlobStore;
class ItemCard;
class TextItemCard;
class Thumbnailer;

// The right pane: the contents of whichever buffer is selected in the list.
//
// This replaces inline expansion. A card sized for a two-line preview could not
// host four images and item-level operations, and the attempt to make it do so
// is what produced "images do not look very good" — the canvas gives an image
// the width of the pane instead of a 52px square crop.
class ItemCanvas : public QScrollArea {
    Q_OBJECT
public:
    ItemCanvas(Thumbnailer& thumbs, BlobStore& blobs, QWidget* parent = nullptr);

    // selectIndex: which item to leave selected afterwards, so a delete does
    // not dump the user back to nothing selected. -1 selects none.
    void setItems(const std::vector<Item>& items, int selectIndex = -1);

    // Narrows the board to the items that matched, and marks the term inside
    // them. An empty query restores everything. `showAll` keeps the filter's
    // marking but stops hiding the rest, for when the surrounding items are the
    // context you actually wanted.
    void setSearch(const QString& query, const std::vector<ItemId>& matching);
    void setShowAll(bool showAll);
    bool isFiltered() const { return !query_.isEmpty() && !showAll_; }
    int  matchCount() const { return int(matching_.size()); }
    int  totalCount() const { return int(allItems_.size()); }
    int  indexOf(ItemId id) const;

    // The widget width this canvas needs before its board settles on `columns`
    // columns — the inverse of stableWidth(), scrollbar and frame included, so
    // a caller sizing a window gets the count it asked for.
    int  widthForColumns(int columns) const;
    // Board order, newest first. Not the same as the widget tree order, which
    // is creation order.
    QList<ItemId> itemOrder() const;
    void showNothingSelected();
    // A buffer that exists but holds nothing yet: Ctrl+N makes one of these and
    // it waits to be pasted into.
    void showEmptyBuffer();
    void clearItems();

    struct DirtyText {
        ItemId  id;      // kNoItem => new text with no row yet
        QString text;
        quint64 generation = 0;
    };
    std::vector<DirtyText> dirtyText() const;
    void markClean();
    // A background save of `id` has landed. Cleans the card only if it has not
    // been edited since; answers whether it did.
    bool markSavedIfUnchanged(ItemId id, quint64 generation, const QString& saved);
    // Tells the cards that just saved to say so.
    void acknowledgeSaved(const QList<ItemId>& ids, Timestamp when);
    // Binds the one unwritten card to the row that was just created for it.
    //
    // NOT by position. An earlier version paired textCards_[i] with the i-th
    // text row of listForBuffer, which assumes widget order equals database
    // order — and editing any card that is not the newest bumps its
    // modified_at, changing the database order while the board deliberately
    // stays put. The next append then rebound every card one slot out, so
    // typing into one note silently overwrote another. There is only ever one
    // composer, so identity is unambiguous and position is never consulted.
    bool bindComposer(ItemId newId);

    // --- keyboard ------------------------------------------------------------
    // The board is fully operable without a mouse: arrows move the cursor,
    // Shift+arrows extend, Space toggles, Enter edits, Home/End jump. Every
    // canvas verb acts on the selection, so without a keyboard route to the
    // selection a keyboard user could select all or nothing — which is how this
    // shipped until an audit drove it and found nothing worked.
    // Arrows move by geometry: Up/Down stay in the column, Left/Right cross to
    // the neighbouring one. Tab is untouched and still walks card to card
    // through Qt's focus chain, which is the "visit everything once" route.
    void moveCursorSpatially(BoardLayout::Step step, Qt::KeyboardModifiers modifiers);
    void setCursorTo(int index, Qt::KeyboardModifiers modifiers);
    int  cursorIndex() const { return cursor_; }
    const BoardLayout& boardForTest() const { return board_; }

    QList<ItemId> selection() const;
    bool hasSelection() const { return !selected_.isEmpty(); }
    void clearSelection();
    void selectAll();

    // Clipboard and editing verbs over the current selection.
    // Not const: a copy acknowledges itself on the card it took.
    void copySelection();
    void cutSelection();       // copies, then asks for removal
    void deleteSelection();

    // Adds an unwritten text card at the top and puts the caret in it. It
    // becomes a real item when it has content, and evaporates if it does not.
    void addPendingTextCard();
    // Starts a note that already holds `firstText` — the keystroke that asked
    // for it, which must land in the note rather than be swallowed.
    void startNote(const QString& firstText);
    // Removes the unwritten note, if there is one. Escape on an empty new note
    // used to leave a blank "Write something…" card behind, complete with a
    // working "Copy text" button (usability test, 2026-09-19).
    void discardComposer();
    // Typing should go into a note: the napkin is empty, or a note has just
    // been started and not yet written. The second case catches keystrokes that
    // arrive before focus has moved into the new note, which would otherwise be
    // dropped after the first letter.
    bool startsNoteOnTyping() const;
    // A napkin (possibly empty) is on the board, as opposed to "Select a napkin".
    bool showingANapkin() const { return bufferShown_; }
    // Showing something in the trash: its cards offer Restore and a Delete that
    // is permanent, and no Cut — nothing can be moved out of the trash but by
    // restoring it (usability test, 2026-10-06: Cut was offered and refused).
    void setInTrash(bool inTrash);
    bool keyboardOnBoard() const { return keyboardOnBoard_; }
    bool inTrash() const { return inTrash_; }
    // Repaints the "5 minutes ago" on every card on the board. Only the list
    // used to tick, so a card said "just now" for as long as it was on screen.
    void refreshTimestamps();
    // Printable text with no command modifier: what "just typing" means.
    static bool isTyping(const QKeyEvent* e);
    bool textHasFocus() const;

    // True when the keyboard is anywhere inside the board. Not hasFocus(),
    // which also requires the window to be active — so it answers false for a
    // background window, and always false under a headless platform.
    bool keyboardIsHere() const;

    // --- zoom ----------------------------------------------------------------
    // The board's own zoom, on top of the application's text size: the cards'
    // font and every dimension the layout decides scale together (BoardLayout::
    // setScale). The canvas carries the zoomed font itself, so the cards inherit
    // it and the existing font-change rebuild re-measures the board.
    static constexpr qreal kMinZoom = 0.7;
    static constexpr qreal kMaxZoom = 2.0;
    void  setZoom(qreal zoom);
    qreal zoom() const { return zoom_; }
    // Ctrl+wheel anywhere on the board, including over a card's text, which
    // never sees it: a read-only QPlainTextEdit — every card not being edited —
    // zooms its own font on Ctrl+wheel, resizing one card's text out from under
    // the height the board measured for it. Checked before any child gets the
    // event (MainWindow's application filter), so it does not depend on the
    // event propagating up. Answers whether it took the event.
    bool takeZoomWheel(QObject* receiver, QWheelEvent* e);
    // --- Spotlight -----------------------------------------------------------
    // One card at a time: a single centred column at reading size, the card in
    // the middle of the view current and the rest dimmed, a rail of ticks
    // beside it. Everything else — editing, selection, copy, the keyboard — is
    // the board's own, because it IS the board, laid out as one column.
    void setSpotlight(bool on);
    bool spotlight() const { return board_.spotlight(); }
    // The card in the middle of the view, which Spotlight treats as current;
    // -1 on the ordinary board.
    int  spotlitIndex() const { return spotlit_; }
    // Scrolls so the card at `index` sits in the middle of the view (its top,
    // if it is taller than the view). Glides when the window is showing.
    void bringToCentre(int index, bool animate = true);

    // The application's text size changed. A zoomed board carries a size of its
    // own, so inheritance does not reach it — and the canvas is not told
    // directly: QApplication::setFont sends ApplicationFontChange only to
    // top-level windows (verified, Qt 6.12), which pass the change down by
    // inheritance. So the window calls this from its own FontChange.
    void followApplicationFont();
    // At most this many board columns; 0 means as many as fit.
    void setColumnCap(int columns);
    int  columnCap() const { return board_.columnCap(); }

    // Ends any card that is being edited. Called whenever attention moves
    // elsewhere — another card, the empty board, another buffer — because
    // leaving a card is a commit.
    void commitEditing();
    bool isEditing() const;

signals:
    void edited();
    // A card stopped being edited. leftEmpty says whether it now holds nothing,
    // which is the ONLY moment an empty card is removed.
    void editingFinished(ItemId id, bool leftEmpty);
    void imagePasted(const QByteArray& bytes, const QString& mime);
    void imageActivated(ItemId id);
    void removeRequested(const QList<ItemId>& ids);
    void cutRequested(const QList<ItemId>& ids);   // copied first, then removed
    void restoreRequested(const QList<ItemId>& ids);   // in the trash: these cards, back
    void selectionChanged();
    void filterChanged();
    // Something happened that no card can speak for on its own — a copy of
    // several items at once, or of one whose card is scrolled out of the band.
    // The window puts it in the toast.
    void announced(const QString& message);
    // Ctrl+wheel over the board: one step in or out. The window owns the zoom
    // (it saves it and says what it changed to), so the canvas only asks.
    void zoomStepRequested(int steps);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

protected:
    // The application font can change under a running window (Settings ▸
    // Appearance). Cards cache font-derived metrics at construction, and the
    // board's measurements were taken against the old face, so both are rebuilt
    // rather than nudged.
    void changeEvent(QEvent* e) override;

private slots:
    // Handing a URL to the desktop is the one thing in Napkin that leaves the
    // machine, so it goes through one function that re-checks the scheme rather
    // than trusting that whatever produced the chip got it right.
    void openUrl(const QString& url);

private:
    // The application's font at zoom_, or the plain inherited font at 100% so
    // an unzoomed board behaves exactly as it did before zoom existed.
    void applyZoomFont();
    // The zoom times Spotlight's step up in type, which is what the font and
    // every layout dimension are scaled by.
    qreal effectiveScale() const;
    qreal zoom_ = 1.0;
    int   wheelAccum_ = 0;

    void updateSpotlit();          // the card nearest the middle of the view
    void placeSpotlightChrome();   // the shade over the body, the rail beside it
    bool gliding() const;
    QWidget* shade_ = nullptr;     // dims every card but the current one
    QWidget* rail_ = nullptr;      // one tick per card; click to jump
    QPropertyAnimation* glide_ = nullptr;
    int spotlit_ = -1;
    void wireCard(ItemCard* card);
    void acknowledgeCopy(const QList<ItemId>& ids);
    // The card for a BOARD index, or null when that item is outside the
    // virtualized band and so has no widget at all.
    ItemCard* liveCardAt(int index) const;
    QString   plainTextFor(const Item& item) const;
    // Writes the clipboard, silently. False means it did not: a lone image
    // whose blob is gone. Shared by copy and cut so the two cannot disagree
    // about what "it worked" means.
    bool      putSelectionOnClipboard(const QList<ItemId>& ids);
    ItemCard* cardAt(const QPoint& viewportPos) const;
    void applySelection(ItemId id, Qt::KeyboardModifiers modifiers);
    void relayout();
    void resizeBody();
    // Places every card that has not been placed yet. Anything that needs the
    // geometry of an arbitrary card — the keyboard cursor — calls this first.
    void finishLayout();
    int  stableWidth() const;

    // Builds widgets for the visible band and destroys the rest. A buffer with
    // 1000 text items used to construct 1000 live QPlainTextEdits: 365 MB peak
    // and 270 ms on every resize event. Only what you can see exists.
    void syncVisibleCards();
    void applyFilter();
    void syncCardText(TextItemCard* card);
    // Unsaved text, by item, held across a rebuild of the board's cards until
    // a card for it exists again or it is saved. See setItems().
    struct Carried {
        QString text;
        quint64 generation = 0;
    };
    QHash<ItemId, Carried> carried_;
    void carryUnsaved();
    ItemCard* cardFor(const Item& item);

    Thumbnailer& thumbs_;
    BlobStore&   blobs_;
    bool bufferShown_      = false;   // a napkin (possibly empty) is on the board
    bool emptyBufferShown_ = false;   // ...and it has nothing on it yet
    QWidget*     body_ = nullptr;
    BoardLayout    board_;
    QTimer*        layoutTimer_ = nullptr;
    std::vector<Item> allItems_;              // everything in the buffer
    std::vector<Item> items_;                 // what the board is showing
    QString           query_;
    QSet<ItemId>      matching_;
    bool              showAll_ = false;
    QHash<ItemId, ItemCard*> live_;           // the cards that currently exist
    QLabel*      placeholder_ = nullptr;
    bool         inTrash_ = false;
    bool         keyboardOnBoard_ = false;
    void         setKeyboardOnBoard(bool here);

    std::vector<ItemCard*>     cards_;
    std::vector<TextItemCard*> textCards_;
    QSet<ItemId> selected_;
    ItemId       anchor_ = kNoItem;   // for Shift+click ranges
    int          cursor_ = -1;        // the keyboard cursor
};

}  // namespace napkin
