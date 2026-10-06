#include "MainWindow.h"
#include "Autosave.h"
#include "BufferListModel.h"
#include "BufferListView.h"
#include "ItemCanvas.h"
#include "Tokens.h"
#include "Lightbox.h"
#include "../media/Exporter.h"
#include "SettingsDialog.h"
#include "TrayIcon.h"
#include "EmptyStateView.h"
#include "WelcomeView.h"
#include "SweepDialog.h"
#include "UndoToast.h"

#include "../data/BufferRepository.h"
#include "../data/Database.h"
#include "../data/ItemRepository.h"
#include "../domain/BufferService.h"
#include "../domain/Clock.h"
#include "../domain/Preview.h"
#include "Icons.h"
#include "NapkinStyle.h"
#include "SurfacePanel.h"
#include "TourDialog.h"
#include "../domain/Search.h"
#include "../domain/TimeFormat.h"
#include "../app/Paths.h"
#include "../media/BlobGc.h"
#include "BackgroundSaver.h"
#include "../app/GlobalShortcut.h"
#include "../media/BlobStore.h"
#include "../media/ClipboardContent.h"
#include "../media/ImageFormats.h"
#include "../media/Thumbnailer.h"

#include <QCloseEvent>
#include <QLabel>
#include <QAction>
#include <memory>
#include <QApplication>
#include <QInputDialog>
#include <QMouseEvent>
#include <QClipboard>
#include <QFileDialog>
#include <QMenu>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QActionGroup>
#include <QButtonGroup>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QMimeData>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardPaths>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScreen>
#include <algorithm>
#include <limits>

namespace napkin {

namespace {

// The track of the Home / Trash switch: a pill of the surface colour that the
// chosen segment is cut out of (NapkinStyle draws the segments).
class SwitchTrack : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(r, r.height() / 2, r.height() / 2);
        p.fillPath(path, palette().color(QPalette::Button));
    }
};

QToolButton* roundButton(const QString& name, const QString& label)
{
    auto* b = new QToolButton;
    b->setObjectName(name);
    b->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("circle"));
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setFixedSize(tokens::kHeaderControlH, tokens::kHeaderControlH);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    b->setToolTip(label);
    b->setAccessibleName(label);
    return b;
}

}  // namespace

namespace {

// Above this many characters a timed autosave is written on the worker. A
// 256 KB note saves in about 3 ms; it is the multi-megabyte ones that froze.
constexpr qsizetype kBackgroundSaveChars = 256 * 1024;

// trimmed().isEmpty() without copying the whole note to find out.
bool isBlank(const QString& text)
{
    for (const QChar c : text)
        if (!c.isSpace()) return false;
    return true;
}

}  // namespace

MainWindow::MainWindow(Database& db, BufferRepository& buffers, ItemRepository& items,
                       BufferService& service, BlobStore& blobs, Thumbnailer& thumbs,
                       QWidget* parent)
    : QMainWindow(parent), db_(db), buffers_(buffers), items_(items), service_(service),
      blobs_(blobs), thumbs_(thumbs)
{
    buildUi();
    applyTraySetting();   // the setting is read at startup, not only on change
    applyShortcutSetting();
}

void MainWindow::buildUi()
{
    model_ = new BufferListModel(db_, buffers_, items_, this);
    view_  = new BufferListView(thumbs_, blobs_);
    view_->installEventFilter(this);   // typing on an empty napkin; see eventFilter
    view_->setModel(model_);

    // --- the start page ------------------------------------------------------
    welcome_ = new WelcomeView;
    welcome_->setContentsMargins(0, 0, tokens::kWindowMargin, 0);   // centred on the window, as the empty states
    connect(welcome_, &WelcomeView::tourRequested, this, &MainWindow::showTour);
    connect(welcome_, &WelcomeView::newBufferRequested, this, &MainWindow::newDraft);
    connect(welcome_, &WelcomeView::pasteRequested, this, &MainWindow::pasteFromClipboard);
    connect(welcome_, &WelcomeView::newTextRequested, this, [this] {
        newDraft();
        canvas_->addPendingTextCard();
    });
    connect(welcome_, &WelcomeView::addImageRequested, this, &MainWindow::addImageFromFile);
    connect(welcome_, &WelcomeView::textTyped, this, [this](const QString& text) {
        // Only the first keystroke makes the napkin; a second one that arrives
        // before focus has moved must join the note, not replace it.
        if (!(model_->hasDraft() && canvas_->startsNoteOnTyping())) newDraft();
        canvas_->startNote(text);
    });
    connect(welcome_, &WelcomeView::searchRequested, this, [this] {
        search_->setFocus(Qt::ShortcutFocusReason);
    });

    // Not "you have nothing yet", so not the start page — but a bare line of
    // text would leave you on a screen with nothing to do and no way back.
    emptyState_ = new EmptyStateView;
    // The page has the window margin on its left only (the board supplies its
    // own on the right), so it is given the same on the right, and what it
    // centres is centred on the window.
    emptyState_->setContentsMargins(0, 0, tokens::kWindowMargin, 0);
    connect(emptyState_, &EmptyStateView::actionTriggered, this, &MainWindow::goHome);

    canvas_ = new ItemCanvas(thumbs_, blobs_);

    // Says what the board is hiding and offers the way back. Filtering without
    // saying so would make a buffer look like it had lost its contents.
    filterBanner_ = new QWidget;
    auto* bannerRow = new QHBoxLayout(filterBanner_);
    bannerRow->setContentsMargins(tokens::kPadX, 8, tokens::kPadX, 8);
    filterLabel_ = new QLabel;
    auto* showAll = new QPushButton(tr("Show all"));
    showAll->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("pill"));
    showAll->setCursor(Qt::PointingHandCursor);
    showAll->setObjectName(QStringLiteral("showAllButton"));
    connect(showAll, &QPushButton::clicked, this, [this] { canvas_->setShowAll(true); });
    bannerRow->addWidget(filterLabel_);
    bannerRow->addStretch();
    bannerRow->addWidget(showAll);
    filterBanner_->hide();

    auto* canvasSide = new QWidget;
    auto* canvasColumn = new QVBoxLayout(canvasSide);
    canvasColumn->setContentsMargins(0, 0, 0, 0);
    canvasColumn->setSpacing(0);
    canvasColumn->addWidget(filterBanner_);
    canvasColumn->addWidget(canvas_, 1);

    connect(canvas_, &ItemCanvas::filterChanged, this, [this, showAll] {
        const bool filtered = canvas_->isFiltered();
        const bool searching = model_->isSearching() && canvas_->matchCount() > 0;
        filterBanner_->setVisible(searching);
        showAll->setVisible(filtered);
        // Singular and plural spelled out: no translation is loaded, so Qt's
        // %n plural forms print "(s)" literally — "1 of 1 items match" and
        // "Delete 1 napkin(s)" both came from that.
        const int n = canvas_->matchCount();
        const int total = canvas_->totalCount();
        const QString q = model_->query();
        filterLabel_->setText(
            filtered ? (total == 1 ? tr("This item matches “%1”").arg(q)
                        : n == 1   ? tr("1 of %1 items matches “%2”").arg(total).arg(q)
                                   : tr("%1 of %2 items match “%3”").arg(n).arg(total).arg(q))
                     : (n == 1 ? tr("Showing every item; 1 matches “%1”").arg(q)
                               : tr("Showing every item; %1 match “%2”").arg(n).arg(q)));
    });

    // The nudge. Cleaning up is never automatic and never silent, so the only
    // thing that happens on its own is this one quiet line appearing.
    sweepNudge_ = new QWidget;
    auto* nudgeRow = new QHBoxLayout(sweepNudge_);
    nudgeRow->setContentsMargins(16, 8, 16, 8);
    sweepLabel_ = new QLabel;
    auto* review = new QPushButton(tr("Review"));
    review->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("pill"));
    review->setCursor(Qt::PointingHandCursor);
    review->setObjectName(QStringLiteral("sweepReviewButton"));
    auto* dismissNudge = new QPushButton(QStringLiteral("✕"));
    dismissNudge->setFlat(true);
    dismissNudge->setAccessibleName(tr("Dismiss"));
    dismissNudge->setFixedSize(tokens::kFieldHeight, tokens::kFieldHeight);   // a round target, not a sliver
    dismissNudge->setCursor(Qt::PointingHandCursor);
    dismissNudge->setToolTip(tr("Not now"));
    dismissNudge->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("circle"));
    connect(review, &QPushButton::clicked, this, &MainWindow::reviewSweep);
    connect(dismissNudge, &QPushButton::clicked, this, [this] {
        nudgeDismissed_ = true;
        sweepNudge_->hide();
    });
    nudgeRow->addWidget(sweepLabel_);
    nudgeRow->addStretch();
    nudgeRow->addWidget(review);
    nudgeRow->addWidget(dismissNudge);
    sweepNudge_->hide();

    // The sidebar is a surface on the window, as the board's cards are; the
    // list inside it is transparent and its rows are drawn on the panel.
    auto* listSide = new SurfacePanel;
    listSide->setObjectName(QStringLiteral("sidebar"));
    auto* listColumn = new QVBoxLayout(listSide);
    listColumn->setContentsMargins(0, tokens::kGapTight, 0, tokens::kGapTight);
    listColumn->setSpacing(0);
    view_->setAutoFillBackground(false);
    view_->viewport()->setAutoFillBackground(false);
    // No bar in the sidebar while in the trash (removed at the user's request,
    // 2026-10-06). It existed because the way out used to be the same "Trash"
    // toggle that led in, which a test user did not recognise as an exit; the
    // way out is now the Home half of the switch beside it. Alt+Left and the
    // mouse's Back button still work, and the retention period is the Trash
    // segment's tooltip.
    listColumn->addWidget(sweepNudge_);
    listColumn->addWidget(view_, 1);

    // The list keeps its own width; the canvas takes the rest. Below ~820px the
    // splitter lets the user collapse either side rather than cramming both.
    splitter_ = new QSplitter(Qt::Horizontal);
    splitter_->addWidget(listSide);
    splitter_->addWidget(canvasSide);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setChildrenCollapsible(false);
    listSide->setMinimumWidth(260);
    listSide->setMaximumWidth(520);
    canvasSide->setMinimumWidth(tokens::kCardMinWidth + tokens::kPadX * 2);

    // Opening sizes, reused below to size the window itself. The canvas asks
    // for the width that draws two columns rather than a round number, so the
    // first thing a new user sees is a board and not a single column of notes
    // — which is what §1 says Napkin is not.
    const int listStartWidth   = tokens::kSidebarWidth;
    const int canvasStartWidth = canvas_->widthForColumns(2);
    splitter_->setSizes({listStartWidth, canvasStartWidth});

    stack_ = new QStackedWidget;
    stack_->addWidget(splitter_);   // 0: the app
    stack_->addWidget(welcome_);    // 1: nothing here yet
    stack_->addWidget(emptyState_); // 2: empty trash, or a search with no hits

    auto* central = new QWidget;
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(tokens::kHeaderGap);
    rootLayout->addWidget(buildHeaderWidget());
    // The desk's margin on three sides. The right one is the board's own inset
    // (kPadX), so the board's scroll bar can sit at the window's edge.
    auto* body = new QVBoxLayout;
    body->setContentsMargins(tokens::kWindowMargin, 0, 0, tokens::kWindowMargin);
    body->addWidget(stack_);
    rootLayout->addLayout(body, 1);
    setCentralWidget(central);

    // The search field is as wide as the list it searches, and stays so when
    // the sidebar is dragged wider or narrower.
    listSide->widthChanged = [this](int w) { if (search_) search_->setFixedWidth(w); };

    // Each offer holds the blobs its undo would need, until it is taken or
    // expires; the sweep asks the toast what is still held.
    toast_ = new UndoToast(central);


    // --- reclaiming blobs -----------------------------------------------------
    // The thumbnail directory is the Thumbnailer's, not paths::thumbsDir().
    // Those are the same in the application, but a window built over a test's
    // temporary store swept the REAL profile's thumbnails whenever a test
    // emptied the trash — every one its own database did not reference.
    sweeper_ = new BlobSweeper(items_, blobs_, thumbs_.cacheDir(), this);
    sweeper_->setProtectedHashes([this] { return toast_->protectedHashes(); });

    // --- autosave ------------------------------------------------------------
    autosave_ = new Autosave(this);
    autosave_->setFlushHandler([this](bool timed) { flushAndReportFailure(timed); });

    saver_ = new BackgroundSaver(db_, this);
    connect(saver_, &BackgroundSaver::saved, this,
            [this](BufferId buffer, const std::vector<BackgroundSaver::Entry>& entries,
                   Timestamp when) {
                QList<ItemId> clean;
                for (const auto& e : entries)
                    if (canvas_->markSavedIfUnchanged(e.id, e.generation, e.text)) clean << e.id;
                canvas_->acknowledgeSaved(clean, when);
                model_->invalidatePreview(buffer);
                saveFailures_ = 0;
            });
    connect(saver_, &BackgroundSaver::failed, this, [this](const QString&) {
        // The cards were never marked clean, so the text is still waiting to be
        // written; the next attempt picks it up.
        ++saveFailures_;
        reportSaveFailure();
    });

    connect(view_, &BufferListView::rowActivated, this, &MainWindow::openRow);
    // A click always opens what was clicked. Selection changes cover most of
    // it, but clicking the row that is already current changes nothing, so a
    // board that had been blanked could not be brought back by clicking.
    connect(view_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        if (model_->idAt(index.row()) != editingBuffer_ || !canvas_->showingANapkin())
            selectBuffer(index.row());
    });
    connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                selectBuffer(current.isValid() ? current.row() : -1);
            });
    connect(canvas_, &ItemCanvas::editingFinished, this,
            [this](ItemId id, bool leftEmpty) {
                // Save what is there, then — and only then — decide whether an
                // empty card should go.
                flushAndReportFailure();
                if (!leftEmpty) return;
                if (id == kNoItem) {
                    // An unwritten note left empty goes away. Queued: the card
                    // is still inside its own event handler.
                    QMetaObject::invokeMethod(this, [this] { canvas_->discardComposer(); },
                                              Qt::QueuedConnection);
                    return;
                }
                QMetaObject::invokeMethod(this, [this, id] { discardItems({id}); },
                                          Qt::QueuedConnection);
            });
    connect(canvas_, &ItemCanvas::imageActivated, this, &MainWindow::openImageItem);
    connect(canvas_, &ItemCanvas::announced, this,
            [this](const QString& message) { toast_->inform(message); });
    connect(canvas_, &ItemCanvas::removeRequested, this,
            [this](const QList<ItemId>& ids) { removeItems(ids, false); });
    connect(canvas_, &ItemCanvas::cutRequested, this,
            [this](const QList<ItemId>& ids) { removeItems(ids, true); });
    connect(canvas_, &ItemCanvas::imagePasted, this,
            [this](const QByteArray& bytes, const QString& mime) {
                addImageToCurrent(bytes, mime, QString());
            });
    connect(canvas_, &ItemCanvas::edited, this, [this] {
        // Freeze on the first keystroke: autosave is about to bump modified_at,
        // and the card you are typing into must not leap to the top.
        model_->freezeOrder(true);
        autosave_->noteChange();
        if (editingBuffer_ == kNoBuffer) {
            Draft d;
            for (const auto& dirty : canvas_->dirtyText()) d.setText(dirty.text);
            model_->setDraftPreview(derivePreview(d.items(), int(d.items().size()), 0));
        }
    });

    connect(model_, &BufferListModel::countChanged, this, [this] {
        updateEmptyState();
        updateSweepNudge();
    });

    // Searching on every keystroke is affordable — 5 ms across 2000 buffers —
    // but a short debounce keeps a fast typist from re-querying mid-word.
    searchDebounce_ = new QTimer(this);
    searchDebounce_->setSingleShot(true);
    searchDebounce_->setInterval(120);
    connect(searchDebounce_, &QTimer::timeout, this, [this] {
        canvas_->commitEditing();
        // Stay on the buffer you are looking at if it survives the change.
        // Clearing a search used to throw you onto whatever was top of the
        // restored list, which loses your place for no reason.
        const BufferId wasOn = editingBuffer_;
        model_->setQuery(search_->text());
        updateEmptyState();

        if (model_->rowCount() == 0) { selectBuffer(-1); return; }
        const int keep = wasOn == kNoBuffer ? -1 : model_->rowForId(wasOn);
        view_->setCurrentIndex(model_->index(keep >= 0 ? keep : 0, 0));
        // currentRowChanged does not fire when the row index is unchanged, so
        // the board is refreshed explicitly for the new query.
        selectBuffer(view_->currentIndex().row());
    });
    connect(search_, &QLineEdit::textChanged, this,
            [this] { searchDebounce_->start(); });


    // --- actions --------------------------------------------------------------
    // Back, as a browser or file manager has it. Enabled only in the trash, so
    // it never takes Alt+Left from anywhere it might mean something else.
    leaveTrashAction_ = new QAction(tr("Back to napkins"), this);
    leaveTrashAction_->setObjectName(QStringLiteral("leaveTrashAction"));
    leaveTrashAction_->setShortcut(QKeySequence::Back);
    leaveTrashAction_->setShortcutContext(Qt::WindowShortcut);
    leaveTrashAction_->setEnabled(false);
    connect(leaveTrashAction_, &QAction::triggered, this, &MainWindow::goHome);
    addAction(leaveTrashAction_);
    // The mouse's Back button, anywhere in the window. Watched at the
    // application, because the widget under the pointer — the list, a card —
    // takes the press before the window would see it.
    qApp->installEventFilter(this);

    // An action rather than a bare shortcut: it carries its own label and key
    // hint, so the binding is discoverable and can be surfaced in a menu later
    // without rewiring anything.
    auto* newBufferAction = new QAction(tr("New napkin"), this);
    newBufferAction->setObjectName(QStringLiteral("newBufferAction"));
    newBufferAction->setShortcut(QKeySequence::New);
    newBufferAction->setShortcutContext(Qt::WindowShortcut);
    connect(newBufferAction, &QAction::triggered, this, &MainWindow::newDraft);
    addAction(newBufferAction);

    auto* pasteAction = new QAction(tr("Paste"), this);
    pasteAction->setObjectName(QStringLiteral("pasteAction"));
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setShortcutContext(Qt::WindowShortcut);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::pasteFromClipboard);
    addAction(pasteAction);

    auto* findAction = new QAction(tr("Search"), this);
    findAction->setObjectName(QStringLiteral("findAction"));
    findAction->setShortcuts({QKeySequence::Find, QKeySequence(QStringLiteral("Ctrl+K"))});
    findAction->setShortcutContext(Qt::WindowShortcut);
    connect(findAction, &QAction::triggered, this, [this] {
        search_->setFocus(Qt::ShortcutFocusReason);
        search_->selectAll();
    });
    addAction(findAction);

    auto* addTextAction = new QAction(tr("New note"), this);
    addTextAction->setObjectName(QStringLiteral("addTextAction"));
    addTextAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    addTextAction->setShortcutContext(Qt::WindowShortcut);
    connect(addTextAction, &QAction::triggered, this, [this] { appendTextBlock(); });
    addAction(addTextAction);

    auto* addImageAction = new QAction(tr("Add image…"), this);
    addImageAction->setObjectName(QStringLiteral("addImageAction"));
    addImageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    addImageAction->setShortcutContext(Qt::WindowShortcut);
    connect(addImageAction, &QAction::triggered, this, &MainWindow::addImageFromFile);
    addAction(addImageAction);

    connect(view_, &BufferListView::trashRequested,      this, &MainWindow::trashRow);
    connect(view_, &BufferListView::restoreRequested,    this, &MainWindow::restoreRow);
    connect(view_, &BufferListView::contextMenuRequested, this, &MainWindow::showContextMenu);

    buildAppMenu();

    // A reload resets the list and clears its current row without a word —
    // QItemSelectionModel resets silently — and a deferred re-sort reloads from
    // inside the model, out of MainWindow's sight. Then Qt's own list view
    // picks its first row when it next gains focus, which is not the latest.
    // So every reset is followed, a turn later, by the one rule.
    connect(model_, &QAbstractItemModel::modelReset, this,
            [this] { QTimer::singleShot(0, this, &MainWindow::selectLatestIfNone); });

    // Relative labels go stale silently, so repaint them on a slow tick.
    timeRefresh_ = new QTimer(this);
    timeRefresh_->setInterval(kTimeRefreshMs);
    connect(timeRefresh_, &QTimer::timeout, this, [this] {
        model_->refreshTimestamps();
        canvas_->refreshTimestamps();
    });
    timeRefresh_->start();

    view_->setAccessibleName(tr("Napkins"));
    view_->setAccessibleDescription(
        tr("Your napkins, newest first. Enter opens one; typing writes on it; Ctrl+P pins, Ctrl+D keeps, Delete trashes."));

    setWindowTitle(tr("Napkin"));
    // The opening size only. The minimum is still one column plus the list, so
    // the window can be dragged narrow or tiled to half a small screen; this
    // just refuses to *start* there. Clamped to the screen, because a 1162px
    // opening width on a 1024px display is a window with its edge off-screen.
    {
        // The window's own margin too: without it the board opened 16px short
        // of its second column.
        const int startWidth  = tokens::kWindowMargin + listStartWidth + canvasStartWidth
                                + splitter_->handleWidth();
        const int startHeight = 760;
        const QRect avail = QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()->availableGeometry()
                                : QRect(0, 0, startWidth, startHeight);
        resize(std::min(startWidth, avail.width()), std::min(startHeight, avail.height()));
    }
    updateEmptyState();

    // A keyboard user arriving with focus on the Trash button and no row
    // selected could press P, K, Delete or Enter and have nothing happen at all.
    selectLatestIfNone();
    view_->setFocus(Qt::OtherFocusReason);
    updateSweepNudge();
}

// The header, after the 2026-10-06 mockup: search on the left, as wide as the
// sidebar it searches; the Home / Trash switch in the middle of the window;
// New, Settings and the menu as round buttons on the right. No menu bar — the
// menu button holds every action (buildAppMenu).
QWidget* MainWindow::buildHeaderWidget()
{
    auto* header = new QWidget;
    auto* grid = new QGridLayout(header);
    grid->setContentsMargins(tokens::kWindowMargin, tokens::kHeaderTop, tokens::kWindowMargin, 0);
    grid->setHorizontalSpacing(tokens::kWindowMargin);
    // Equal outer columns keep the switch centred on the window, whatever the
    // search field and the buttons happen to measure.
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(2, 1);

    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search through napkins"));
    search_->setClearButtonEnabled(true);
    search_->setObjectName(QStringLiteral("searchField"));
    search_->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("pill"));
    search_->setFixedHeight(tokens::kHeaderControlH);
    search_->setFixedWidth(tokens::kSidebarWidth);
    search_->setTextMargins(4, 0, 8, 0);
    searchGlyph_ = search_->addAction(QIcon(), QLineEdit::LeadingPosition);
    // The magnifier is drawn as a button, so it has to say what it is; pressing
    // it puts the caret in the field, which is what a click there means.
    searchGlyph_->setText(tr("Search"));
    // The action's associated objects include the field itself, not only the
    // glyph's button: taking focus away from all of them made the search field
    // unclickable — only Ctrl+F, which sets focus directly, could reach it.
    for (QObject* o : searchGlyph_->associatedObjects())
        if (auto* b = qobject_cast<QAbstractButton*>(o)) {
            b->setAccessibleName(tr("Search"));
            b->setFocusPolicy(Qt::NoFocus);
        }
    connect(searchGlyph_, &QAction::triggered, this,
            [this] { search_->setFocus(Qt::MouseFocusReason); });
    search_->installEventFilter(this);   // Ctrl+V here; see eventFilter
    search_->setAccessibleName(tr("Search your napkins"));
    search_->setToolTip(tr("Search your napkins (Ctrl+F)\n"
                           "Ctrl+V pastes onto the napkin; Ctrl+Shift+V pastes here."));
    grid->addWidget(search_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    // Home and Trash are two places, so they are two segments of one switch:
    // the way out of the trash is the other half of the way in. The old single
    // "Trash" toggle had to be noticed as pressed before it read as a way back.
    auto* track = new SwitchTrack;
    track->setObjectName(QStringLiteral("placeSwitch"));
    auto* trackRow = new QHBoxLayout(track);
    trackRow->setContentsMargins(0, 0, 0, 0);
    trackRow->setSpacing(0);
    auto segment = [](const QString& label, const QString& name) {
        auto* b = new QPushButton(label);
        b->setObjectName(name);
        b->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("segment"));
        b->setCheckable(true);
        b->setFlat(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(94, tokens::kHeaderControlH);
        return b;
    };
    homeSegment_ = segment(tr("Home"), QStringLiteral("homeSegment"));
    homeSegment_->setChecked(true);
    homeSegment_->setToolTip(tr("Your napkins (Ctrl+Home)"));
    homeSegment_->setAccessibleName(tr("Show your napkins"));
    auto* trashButton = segment(tr("Trash"), QStringLiteral("trashToggle"));
    trashButton->setAccessibleName(tr("Show trash"));
    trashButton->setToolTip(tr("Deleted napkins and items stay here for %1 days.")
                                .arg(BufferService::trashRetentionDays()));
    trackRow->addWidget(homeSegment_);
    trackRow->addWidget(trashButton);
    grid->addWidget(track, 0, 1, Qt::AlignCenter);

    // One place at a time, and pressing the place you are in keeps you there.
    // Free checkable buttons let a second click on "Trash" un-press it and
    // leave the trash (independent review, 2026-10-06).
    auto* places = new QButtonGroup(track);
    places->setExclusive(true);
    places->addButton(homeSegment_);
    places->addButton(trashButton);

    trashToggle_ = trashButton;
    connect(trashButton, &QPushButton::toggled, this, [this](bool on) { showTrash(on); });

    auto* right = new QWidget;
    auto* rightRow = new QHBoxLayout(right);
    rightRow->setContentsMargins(0, 0, 0, 0);
    rightRow->setSpacing(tokens::kWindowMargin);

    emptyTrashButton_ = new QPushButton(tr("Empty trash…"));   // it asks first, as the menu item does
    emptyTrashButton_->setFlat(true);
    emptyTrashButton_->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("pill"));
    emptyTrashButton_->setFixedHeight(tokens::kHeaderControlH);
    emptyTrashButton_->setCursor(Qt::PointingHandCursor);
    emptyTrashButton_->setObjectName(QStringLiteral("emptyTrashButton"));
    emptyTrashButton_->hide();   // only meaningful while looking at the trash
    connect(emptyTrashButton_, &QPushButton::clicked, this, &MainWindow::emptyTrash);
    rightRow->addWidget(emptyTrashButton_);

    newButton_ = roundButton(QStringLiteral("newButton"), tr("New napkin (Ctrl+N)"));
    newButton_->setAccessibleName(tr("New napkin"));
    connect(newButton_, &QToolButton::clicked, this, &MainWindow::newDraft);
    rightRow->addWidget(newButton_);

    // Settings in one click. It lived only in the menu bar, which Global Menu
    // (Plasma) moves out of the window altogether.
    settingsButton_ = roundButton(QStringLiteral("settingsButton"), tr("Settings"));
    connect(settingsButton_, &QToolButton::clicked, this, &MainWindow::openSettings);
    rightRow->addWidget(settingsButton_);

    // Every action, behind one button. Populated in buildAppMenu(), once the
    // actions exist.
    overflowButton_ = roundButton(QStringLiteral("overflowButton"), tr("Menu (F10)"));
    overflowButton_->setAccessibleName(tr("Menu"));
    overflowButton_->setPopupMode(QToolButton::InstantPopup);
    rightRow->addWidget(overflowButton_);

    grid->addWidget(right, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    sizeHeaderControls();
    return header;
}

// Every action the application has, in one menu behind the header's menu
// button — the one place a user can go to find out what it can do. The header
// buttons and the key chords are shortcuts to these, not a separate set.
//
// It replaced the menu bar (2026-10-06 mockup). Sections stand in for the old
// File / Napkins / Trash / Help menus, so nothing moved further than one menu
// away. Every action with a shortcut is also the window's own action, so the
// chord works whether or not the menu has ever been opened. F10 opens it.
void MainWindow::buildAppMenu()
{
    styleToolbarIcons();
    auto named = [this](const char* name) -> QAction* {
        return findChild<QAction*>(QString::fromLatin1(name));
    };
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("appMenu"));
    menu->setToolTipsVisible(true);
    appMenu_ = menu;
    // Bound to the window as well, so the chord is live without the menu.
    auto bind = [this](QAction* a) { if (!a->shortcut().isEmpty()) addAction(a); };

    menu->addAction(named("newBufferAction"));
    menu->addAction(named("addTextAction"));
    menu->addAction(named("addImageAction"));
    menu->addAction(named("pasteAction"));

    menu->addSection(tr("Napkins"));
    // Ctrl+Z takes back the last delete, clean-up or cut — whatever the toast
    // is offering. A caret in a note or the search box keeps Ctrl+Z for its own
    // text: Qt gives the focused editor first refusal on the shortcut.
    auto* undo = menu->addAction(tr("Undo"));
    undo->setObjectName(QStringLiteral("undoAction"));
    undo->setShortcut(QKeySequence::Undo);
    bind(undo);
    connect(undo, &QAction::triggered, this, [this] { toast_->undoNow(); });
    // Greyed out only while the menu is open: a disabled action's shortcut does
    // not fire, so leaving it disabled would kill Ctrl+Z until the next visit.
    connect(menu, &QMenu::aboutToShow, this, [this, undo] { undo->setEnabled(toast_->hasOffer()); });
    connect(menu, &QMenu::aboutToHide, undo, [undo] { undo->setEnabled(true); });
    auto* pin = menu->addAction(tr("Pin or unpin"));
    pin->setObjectName(QStringLiteral("pinAction"));
    pin->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));   // Napkin prints nothing
    pin->setToolTip(tr("Pinned napkins stay at the top of the list."));
    bind(pin);
    connect(pin, &QAction::triggered, this, [this] { togglePin(view_->currentIndex().row()); });
    auto* keep = menu->addAction(tr("Keep or release"));
    keep->setObjectName(QStringLiteral("keepAction"));
    keep->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));  // the "bookmark" key, like Keep's glyph
    keep->setToolTip(tr("Clean up never moves a kept napkin to the trash."));
    bind(keep);
    connect(keep, &QAction::triggered, this, [this] { toggleKeep(view_->currentIndex().row()); });
    auto* rename = menu->addAction(tr("Rename…"));
    rename->setObjectName(QStringLiteral("renameAction"));
    rename->setShortcut(QKeySequence(Qt::Key_F2));
    rename->setToolTip(tr("Give this napkin a name of its own. Leave it empty to title it "
                          "from its first note again."));
    bind(rename);
    connect(rename, &QAction::triggered, this, [this] { renameRow(view_->currentIndex().row()); });
    auto* showAll = menu->addAction(tr("All napkins"));
    showAll->setObjectName(QStringLiteral("homeAction"));
    showAll->setShortcut(QKeySequence(QStringLiteral("Ctrl+Home")));
    bind(showAll);
    connect(showAll, &QAction::triggered, this, &MainWindow::goHome);
    menu->addAction(named("findAction"));
    auto* cleanUp = menu->addAction(tr("Clean up…"));
    connect(cleanUp, &QAction::triggered, this, &MainWindow::reviewSweep);

    menu->addSection(tr("Trash"));
    // A plain command, not a checkbox: the switch in the header already says
    // where you are, and a check gutter pushed every item in the menu sideways.
    showTrashAction_ = menu->addAction(tr("Show trash"));
    connect(showTrashAction_, &QAction::triggered, this, [this] {
        if (trashToggle_) trashToggle_->setChecked(true);
        else              showTrash(true);
    });
    auto* restore = menu->addAction(tr("Restore selected"));
    connect(restore, &QAction::triggered, this,
            [this] { restoreRow(view_->currentIndex().row()); });
    auto* empty = menu->addAction(tr("Empty trash…"));
    connect(empty, &QAction::triggered, this, &MainWindow::emptyTrash);
    connect(menu, &QMenu::aboutToShow, this, [this, restore, empty] {
        const bool inTrash = model_->mode() == BufferListModel::Mode::Trash;
        restore->setEnabled(inTrash && view_->currentIndex().isValid());
        empty->setEnabled(buffers_.countTrash() > 0);
    });

    menu->addSeparator();
    exportBufferAction_ = menu->addAction(tr("Export this napkin…"));
    connect(exportBufferAction_, &QAction::triggered, this, &MainWindow::exportCurrentBuffer);
    auto* exportAll = menu->addAction(tr("Export everything…"));
    connect(exportAll, &QAction::triggered, this, &MainWindow::exportEverything);
    // Settings is called what its dialog is called.
    auto* prefs = menu->addAction(tr("Settings…"));
    prefs->setShortcut(QKeySequence::Preferences);
    bind(prefs);
    connect(prefs, &QAction::triggered, this, &MainWindow::openSettings);
    // The theme, one step from anywhere: switching light and dark is the
    // setting people change most, and it should not need a dialog and a Save.
    auto* themeMenu = menu->addMenu(tr("Theme"));
    themeMenu->setObjectName(QStringLiteral("themeMenu"));
    auto* themes = new QActionGroup(themeMenu);
    themes->setExclusive(true);
    const std::pair<SettingsDialog::Theme, QString> choices[] = {
        {SettingsDialog::Theme::System, tr("Follow the system")},
        {SettingsDialog::Theme::Light,  tr("Light")},
        {SettingsDialog::Theme::Dark,   tr("Dark")},
    };
    for (const auto& [theme, label] : choices) {
        auto* a = themeMenu->addAction(label);
        a->setCheckable(true);
        a->setData(int(theme));
        themes->addAction(a);
        connect(a, &QAction::triggered, this, [theme] { SettingsDialog::setTheme(theme); });
    }
    // Read when shown, so a change made in Settings is what the menu says.
    connect(themeMenu, &QMenu::aboutToShow, themeMenu, [themes] {
        for (QAction* a : themes->actions())
            a->setChecked(a->data().toInt() == int(SettingsDialog::theme()));
    });
    // F11, as in browsers and most desktop apps; Ctrl+Meta+F on macOS, which
    // is what QKeySequence::FullScreen means there. Checked while it is on, and
    // kept in step if the window manager changes it (changeEvent).
    fullScreenAction_ = menu->addAction(tr("Full screen"));
    fullScreenAction_->setObjectName(QStringLiteral("fullScreenAction"));
    fullScreenAction_->setCheckable(true);
    // The platform's own binding, plus F11 where that is not already it — once:
    // the same chord twice on one action makes Qt call it ambiguous and fire neither.
    QList<QKeySequence> fullScreenKeys = QKeySequence::keyBindings(QKeySequence::FullScreen);
    if (!fullScreenKeys.contains(QKeySequence(Qt::Key_F11))) fullScreenKeys.prepend(QKeySequence(Qt::Key_F11));
    fullScreenAction_->setShortcuts(fullScreenKeys);
    fullScreenAction_->setShortcutContext(Qt::WindowShortcut);
    addAction(fullScreenAction_);
    connect(fullScreenAction_, &QAction::triggered, this, &MainWindow::setFullScreen);
    auto* tour = menu->addAction(tr("Welcome tour"));
    tour->setObjectName(QStringLiteral("tourAction"));
    connect(tour, &QAction::triggered, this, &MainWindow::showTour);
    auto* shortcuts = menu->addAction(tr("Keyboard shortcuts…"));
    connect(shortcuts, &QAction::triggered, this, &MainWindow::showShortcuts);
    auto* about = menu->addAction(tr("About Napkin"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About Napkin"),
            tr("<b>Napkin</b> %1<br>A persistent scratch surface for your computer."
               "<br><br>Put it here. Use it. Decide later whether it matters."
               "<br><br>Everything stays on this machine. Napkin makes no network "
               "requests.").arg(QCoreApplication::applicationVersion().toHtmlEscaped()));
    });
    menu->addSeparator();
    auto* quit = menu->addAction(tr("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    bind(quit);
    connect(quit, &QAction::triggered, this, [this] { quitNapkin(); });

    overflowButton_->setMenu(menu);

    // The keyboard's way in, where a menu bar's Alt used to be: F10 is the key
    // GNOME and KDE both use for an application's menu.
    auto* openMenu = new QAction(tr("Open the menu"), this);
    openMenu->setObjectName(QStringLiteral("openMenuAction"));
    openMenu->setShortcut(QKeySequence(Qt::Key_F10));
    openMenu->setShortcutContext(Qt::WindowShortcut);
    connect(openMenu, &QAction::triggered, overflowButton_, &QToolButton::showMenu);
    addAction(openMenu);
}

// One gesture back to the ordinary view from wherever you are: out of the
// trash, out of a search, back to the top of the list. Shared by the Napkins menu
// and by every empty state, so they cannot drift apart.
void MainWindow::goHome()
{
    // Cleared immediately rather than through the debounce: an action that
    // takes effect a beat later reads as not having worked.
    search_->clear();
    searchDebounce_->stop();
    model_->setQuery(QString());
    showTrash(false);
    if (homeSegment_) homeSegment_->setChecked(true);   // the group unpresses Trash
    if (model_->rowCount() > 0) view_->setCurrentIndex(model_->index(0, 0));
    // currentRowChanged does not fire when the row is unchanged, and showTrash
    // above blanked the board — which left a napkin highlighted in the list
    // with "Select a napkin" beside it, and clicking it did nothing.
    selectBuffer(view_->currentIndex().row());
    view_->setFocus(Qt::OtherFocusReason);
    updateEmptyState();
}

// SPEC.md §13. A folder of ordinary files, not an archive format only Napkin
// can open: the point of an export is to be readable by something that is not
// this application, including by a person with a file manager.
void MainWindow::exportCurrentBuffer()
{
    const int row = view_->currentIndex().row();
    const BufferId id = row >= 0 ? model_->idAt(row) : kNoBuffer;
    if (id == kNoBuffer) {
        QMessageBox::information(this, tr("Export"),
                                 tr("Select a napkin first, then export it."));
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Export this napkin to…"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (dir.isEmpty()) return;

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    Exporter exporter(buffers_, items_, blobs_);
    const auto result = exporter.exportBuffer(id, dir);
    QGuiApplication::restoreOverrideCursor();

    reportExport(result, tr("This napkin"));
}

void MainWindow::exportEverything()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Export everything to…"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (dir.isEmpty()) return;

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    Exporter exporter(buffers_, items_, blobs_);
    const auto result = exporter.exportAll(dir);
    QGuiApplication::restoreOverrideCursor();

    reportExport(result, tr("Everything"));
}

void MainWindow::reportExport(const Exporter::Result& result, const QString& what)
{
    if (!result.ok) {
        // §14: plain language, and say what happened to the content.
        QMessageBox::warning(this, tr("Export"),
                             tr("%1 could not be exported.\n\n%2\n\n"
                                "Nothing in Napkin has been changed.")
                                 .arg(what, result.error));
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Export"));
    box.setText(tr("%1 was exported.").arg(what));
    box.setInformativeText((result.items == 1 ? tr("1 item written to:\n%1")
                                              : tr("%1 items written to:\n%2").arg(result.items))
                               .arg(QDir::toNativeSeparators(result.rootDir)));

    // An export that skipped something has to say so where the user is already
    // looking. A backup that quietly is not one is worse than no backup.
    if (!result.problems.isEmpty()) {
        box.setIcon(QMessageBox::Warning);
        box.setInformativeText(box.informativeText()
                               + (result.problems.size() == 1
                                      ? tr("\n\n1 item could not be written.")
                                      : tr("\n\n%1 items could not be written.")
                                            .arg(result.problems.size())));
        box.setDetailedText(result.problems.join(QChar(u'\n')));
    } else {
        box.setIcon(QMessageBox::Information);
    }
    box.exec();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this] {
        // Thresholds moved, so what counts as "older" moved with them.
        model_->reload();
        updateSweepNudge();
        applyTraySetting();
        applyShortcutSetting();
    });
    dialog.exec();
}

void MainWindow::showTour()
{
    TourDialog tour(this);
    tour.exec();
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(
        this, tr("Keyboard shortcuts"),
        tr("<table cellpadding='4'>"
           "<tr><td><b>Ctrl+N</b></td><td>New napkin</td></tr>"
           "<tr><td><b>Ctrl+F</b></td><td>Search</td></tr>"
           "<tr><td><b>Ctrl+Shift+V</b></td><td>Paste into the search box</td></tr>"
           "<tr><td><b>Ctrl+T</b></td><td>New note on this napkin</td></tr>"
           "<tr><td><b>Ctrl+V</b></td><td>Paste onto this napkin</td></tr>"
           "<tr><td><b>Ctrl+Shift+I</b></td><td>Add an image from a file</td></tr>"
           "<tr><td><b>Ctrl+P</b></td><td>Pin this napkin — it stays at the top</td></tr>"
           "<tr><td><b>Ctrl+D</b></td><td>Keep this napkin — Clean up never moves it to the trash</td></tr>"
           "<tr><td><b>F2</b></td><td>Rename this napkin (optional)</td></tr>"
           "<tr><td><b>Ctrl+Z</b></td><td>Undo the last delete, pin or keep</td></tr>"
           "<tr><td colspan='2'>&nbsp;</td></tr>"
           "<tr><td colspan='2'><i>On the napkin:</i></td></tr>"
           "<tr><td><b>Click</b></td><td>Select an item</td></tr>"
           "<tr><td><b>Double-click</b></td><td>Edit a note, or open an image</td></tr>"
           "<tr><td><b>Ctrl</b> / <b>Shift</b> + click</td><td>Extend the selection</td></tr>"
           "<tr><td><b>Ctrl+A</b></td><td>Select every item</td></tr>"
           "<tr><td><b>Ctrl+C</b> / <b>Ctrl+X</b> / <b>Delete</b></td>"
           "<td>Copy, cut or delete the selection</td></tr>"
           "<tr><td><b>Double-click</b> empty space</td><td>New note</td></tr>"
           "<tr><td><b>Right-click</b></td><td>Edit, copy, cut or delete</td></tr>"
           "<tr><td><b>Ctrl+Tab</b></td><td>Calculate the line — <i>12*3 =</i> "
           "becomes <i>12*3 = 36</i></td></tr>"
           "<tr><td><b>Ctrl+Enter</b></td><td>Finish editing</td></tr>"
           "<tr><td><b>Esc</b></td><td>Finish editing, then clear the selection</td></tr>"
           "<tr><td colspan='2'>&nbsp;</td></tr>"
           "<tr><td colspan='2'><i>With the list focused:</i></td></tr>"
           "<tr><td><b>Typing</b></td><td>Write on the selected napkin</td></tr>"
           "<tr><td><b>Delete</b></td><td>Move to trash</td></tr>"
           "<tr><td><b>R</b></td><td>Restore (in the trash)</td></tr>"
           "</table>"));
}

void MainWindow::undoLastTrashForTest(BufferId id, bool wasKept, Timestamp modifiedAt)
{
    service_.restore(id);
    if (wasKept) service_.setKept(id, true);
    buffers_.setModifiedAt(id, modifiedAt);
    lastTrashed_ = {};
    reloadPreservingSelection();
}

// The same path the menu takes once the user has confirmed, so a test drives
// what ships rather than a look-alike that skipped the sweep.
void MainWindow::emptyTrashForTest() { emptyTrashConfirmed(); }

void MainWindow::sweepBlobs() { sweeper_->start(); }

QSet<QString> MainWindow::undoProtectedBlobsForTest() const { return toast_->protectedHashes(); }

QString MainWindow::napkinName(BufferId id) const
{
    if (id == kNoBuffer) return tr("a new napkin");
    const auto counts = items_.countsForBuffer(id);
    const auto buffer = buffers_.find(id);
    QString title = derivePreview(items_.previewHead(id), counts.total, counts.images, {},
                                  buffer ? buffer->name : QString()).primary;
    title = title.simplified();
    // Short enough that the toast stays a line; the name is a reminder, not
    // the content.
    if (title.size() > 32) title = title.left(31).trimmed() + QStringLiteral("…");
    return title.isEmpty() ? tr("Untitled") : title;
}

void MainWindow::restoreRow(int row)
{
    if (model_->mode() != BufferListModel::Mode::Trash) return;
    const BufferId id = model_->idAt(row);
    if (id == kNoBuffer) return;
    BufferId target = id;
    if (!guarded(tr("Could not restore that napkin"), [&] { target = service_.restore(id); })) return;

    // The restored napkin has left the trash, so it must leave the board too.
    // It stayed there, beside a list that no longer held it (second
    // usability test) — the list/board disagreement again, by another road.
    if (editingBuffer_ == id) {
        editingBuffer_ = kNoBuffer;
        canvas_->showNothingSelected();
    }
    reloadPreservingSelection();
    updateEmptyTrashButton();

    // Restore used to happen in silence; now it says where things went.
    if (target != id) {
        toast_->inform(tr("Restored to “%1”").arg(napkinName(target)));
    } else {
        toast_->offer(tr("“%1” restored").arg(napkinName(id)), [this, id] {
            guarded(tr("Could not undo that"), [&] { if (!service_.trash(id)) service_.trashConfirmed(id); });
            reloadPreservingSelection();
            updateEmptyTrashButton();
        });
    }
}

// A quiet line, shown only when there is genuinely something to clean up and
// only until it is waved away. Napkin tolerates accumulation; this is an offer,
// not a complaint.
void MainWindow::updateSweepNudge()
{
    if (!sweepNudge_) return;
    const bool relevant = !nudgeDismissed_
                       && !model_->isSearching()
                       && model_->mode() == BufferListModel::Mode::Live;
    const int sweepable = relevant ? model_->sweepableCount() : 0;
    const bool worth = sweepable >= kSweepNudgeThreshold;

    sweepNudge_->setVisible(worth);
    if (worth) {
        // Short: the list pane is narrow, and a nudge that elides mid-word
        // reads as a fault rather than an offer.
        sweepLabel_->setText(tr("%1 over %2 days old")
                                 .arg(sweepable).arg(kOlderThresholdDays));
        sweepLabel_->setToolTip(tr("%1 napkins have not been touched in %2 days")
                                    .arg(sweepable).arg(kOlderThresholdDays));
    }
}

void MainWindow::reviewSweep()
{
    canvas_->commitEditing();
    SweepDialog dialog(buffers_, items_, this);
    // Nothing to review: say so, rather than opening an empty list with a
    // highlighted "Move to trash" (usability test, 2026-09-19).
    if (dialog.candidates() == 0) {
        QMessageBox::information(
            this, tr("Clean up"),
            tr("Nothing to clean up. No napkin has gone untouched for %1 days.")
                .arg(BufferService::olderThanDays()));
        return;
    }
    if (dialog.exec() != QDialog::Accepted) return;
    sweepForTest(dialog.accepted());
}

// The sweep itself, separated from the dialog that chooses what goes into it.
void MainWindow::sweepForTest(const QList<BufferId>& chosen)
{
    if (chosen.isEmpty()) return;

    int swept = 0;
    const bool ok = guarded(tr("Could not clean up"), [&] {
        for (BufferId id : chosen) {
            // trash(), never a hard delete: a sweep must stay undoable, and a
            // kept buffer refuses outright, which is the guarantee (SPEC.md §6).
            if (service_.trash(id)) ++swept;
        }
    });
    if (!ok) return;

    if (editingBuffer_ != kNoBuffer && chosen.contains(editingBuffer_)) {
        editingBuffer_ = kNoBuffer;
        canvas_->showNothingSelected();
    }
    reloadPreservingSelection();
    updateSweepNudge();

    toast_->offer(swept == 1 ? tr("1 napkin moved to trash")
                             : tr("%1 napkins moved to trash").arg(swept), [this, chosen] {
        guarded(tr("Could not undo that"), [&] {
            for (BufferId id : chosen) service_.restore(id);
        });
        reloadPreservingSelection();
        updateSweepNudge();
    });
}

// "Empty trash" belongs to the trash view. Three of the four places that set
// its visibility checked only that the list was non-empty, so emptying from
// the menu left the button on the ordinary napkin list.
void MainWindow::updateEmptyTrashButton()
{
    emptyTrashButton_->setVisible(model_->mode() == BufferListModel::Mode::Trash
                                  && model_->rowCount() > 0);
}

void MainWindow::showTrash(bool trash)
{
    flushAndReportFailure();
    toast_->dismiss();
    model_->setMode(trash ? BufferListModel::Mode::Trash : BufferListModel::Mode::Live);
    updateEmptyTrashButton();
    // Read each time: the retention is a setting.
    if (trashToggle_)
        trashToggle_->setToolTip(tr("Deleted napkins and items stay here for %1 days.")
                                     .arg(BufferService::trashRetentionDays()));
    if (leaveTrashAction_) leaveTrashAction_->setEnabled(trash);
    // Switching modes leaves nothing selected, so the board must stop showing
    // the buffer that was selected in the other one. It did not: entering the
    // trash kept the previous live buffer's cards on screen, and a mode next to
    // deletion that looks identical to ordinary working is the worst kind of
    // ambiguity about which one you are in.
    canvas_->showNothingSelected();
    // ...and nothing must look selected in the list either, or the list and the
    // board disagree about what you are looking at.
    if (view_->currentIndex().isValid()) {
        const QSignalBlocker quiet(view_->selectionModel());
        view_->selectionModel()->clearCurrentIndex();
        view_->viewport()->update();
    }
    editingBuffer_ = kNoBuffer;
    updateEmptyState();
    updateSweepNudge();
    selectLatestIfNone();   // the trash opens on what was deleted last, not on a blank board
}

// "Select a napkin to see what is on it" is a screen with nothing to do on
// it, and the user asked never to land there. Whenever the list has napkins
// and none is open, open the latest: the most recently changed one, or in the
// trash the most recently deleted (the trash lists those first). A draft
// being written counts as open.
void MainWindow::selectLatestIfNone()
{
    if (model_->rowCount() == 0 || model_->hasDraft()) return;
    const int current = view_->currentIndex().row();
    if (current >= 0 && editingBuffer_ != kNoBuffer && canvas_->showingANapkin()) return;

    int latest = 0;
    if (model_->mode() == BufferListModel::Mode::Live && !model_->isSearching()) {
        qint64 newest = std::numeric_limits<qint64>::min();
        for (int row = 0; row < model_->rowCount(); ++row) {
            const qint64 at = model_->index(row, 0).data(BufferListModel::ModifiedAtRole).toLongLong();
            if (at > newest) { newest = at; latest = row; }
        }
    }
    // currentRowChanged does not fire for the row that is already current.
    if (current == latest) selectBuffer(latest);
    else                   view_->setCurrentIndex(model_->index(latest, 0));
}

void MainWindow::reloadPreservingSelection()
{
    // The order freeze exists to stop the list RE-SORTING under someone who is
    // typing. It must not also suppress a change in list MEMBERSHIP: every
    // caller here is a structural change — trash, restore, pin, keep, sweep,
    // undo — and deferring those left a buffer visible in the list while the
    // toast beneath it said the buffer was in the trash.
    model_->freezeOrder(false);

    const BufferId current = model_->idAt(view_->currentIndex().row());
    model_->reload();
    if (const int row = model_->rowForId(current); row >= 0)
        view_->setCurrentIndex(model_->index(row, 0));
    updateEmptyState();
    selectLatestIfNone();   // the napkin that was open is gone: show the latest
}

void MainWindow::togglePin(int row)
{
    const BufferId id = model_->idAt(row);
    if (id == kNoBuffer || model_->mode() != BufferListModel::Mode::Live) return;

    const auto buffer = buffers_.find(id);
    if (!buffer) return;
    const bool was = buffer->pinned;
    const QString name = napkinName(id);
    if (!guarded(tr("Could not pin that napkin"),
                 [&] { service_.setPinned(id, !was); }))
        return;
    reloadPreservingSelection();   // pinning moves the card; that is the point
    // Never silent: a pin changed by a stray key must be seen, and undoable.
    toast_->offer(was ? tr("Unpinned “%1”").arg(name)
                      : tr("Pinned “%1” — it stays at the top").arg(name), [this, id, was] {
        guarded(tr("Could not undo that"), [&] { service_.setPinned(id, was); });
        reloadPreservingSelection();
    });
}

void MainWindow::renameRow(int row)
{
    const BufferId id = model_->idAt(row);
    if (id == kNoBuffer || model_->mode() != BufferListModel::Mode::Live) return;
    const auto buffer = buffers_.find(id);
    if (!buffer) return;

    // The placeholder is the title it has without a name, so clearing the
    // field visibly means "back to that" rather than "no title at all".
    const auto counts = items_.countsForBuffer(id);
    const QString automatic =
        derivePreview(items_.previewHead(id), counts.total, counts.images).primary;

    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Rename napkin"));
    dialog.setLabelText(tr("Name — leave empty to title it from what is on it:"));
    dialog.setTextValue(buffer->name);
    dialog.setOkButtonText(tr("Rename"));   // says what it does, as every Napkin button does
    if (auto* field = dialog.findChild<QLineEdit*>()) {
        field->setPlaceholderText(automatic);
        field->setMaxLength(BufferRepository::kMaxNameLength);
        // QInputDialog sizes itself to its field, so the room goes there: a
        // title of a few words was clipped in a 300px box.
        field->setMinimumWidth(360);
        field->selectAll();
    }
    if (dialog.exec() != QDialog::Accepted) return;
    renameNapkin(id, dialog.textValue());
}

void MainWindow::renameNapkin(BufferId id, const QString& name)
{
    const auto buffer = buffers_.find(id);
    if (!buffer) return;
    const QString was = buffer->name;
    if (name.simplified() == was) return;
    if (!guarded(tr("Could not rename that napkin"), [&] { buffers_.setName(id, name); }))
        return;
    model_->invalidatePreview(id);
    const QString now = napkinName(id);
    toast_->offer(name.simplified().isEmpty() ? tr("“%1” is titled from what is on it again").arg(now)
                                              : tr("Renamed “%1”").arg(now),
                  [this, id, was] {
                      guarded(tr("Could not undo that"), [&] { buffers_.setName(id, was); });
                      model_->invalidatePreview(id);
                  });
}

void MainWindow::toggleKeep(int row)
{
    const BufferId id = model_->idAt(row);
    if (id == kNoBuffer || model_->mode() != BufferListModel::Mode::Live) return;

    const auto buffer = buffers_.find(id);
    if (!buffer) return;
    const bool was = buffer->kept;
    const QString name = napkinName(id);
    if (!guarded(tr("Could not change that napkin"),
                 [&] { service_.setKept(id, !was); }))
        return;
    model_->refreshRow(id);        // keeping changes nothing about placement
    toast_->offer(was ? tr("“%1” is no longer kept").arg(name)
                      : tr("Kept “%1” — Clean up will leave it alone").arg(name),
                  [this, id, was] {
        guarded(tr("Could not undo that"), [&] { service_.setKept(id, was); });
        model_->refreshRow(id);
    });
}

void MainWindow::trashRow(int row)
{
    const BufferId id = model_->idAt(row);
    if (id == kNoBuffer) return;

    if (model_->mode() == BufferListModel::Mode::Trash) {
        // Delete used to mean *restore* here, which is the opposite of what it
        // means in every file manager and mail client. Restore is its own
        // action; Delete destroys, with a confirmation because it is final.
        QMessageBox box(this);
        box.setWindowTitle(tr("Delete permanently?"));
        box.setText(tr("Delete this napkin permanently?"));
        box.setInformativeText(tr("This cannot be undone."));
        box.setIcon(QMessageBox::Warning);
        box.addButton(QMessageBox::Cancel);
        auto* confirm = box.addButton(tr("Delete permanently"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != confirm) return;

        if (!guarded(tr("Could not delete that napkin"), [&] {
                buffers_.hardDeleteEvenIfKept(id);
            }))
            return;
        sweeper_->start();   // reclaim its blobs and thumbnails, off the UI thread
        reloadPreservingSelection();
        updateEmptyTrashButton();
        return;
    }

    const QString name = napkinName(id);
    // The repository refuses a kept buffer outright, so the confirmation cannot
    // be skipped by a UI path that forgets to ask (SPEC.md §6).
    if (const auto before = buffers_.find(id))
        lastTrashed_ = {id, before->kept, before->modifiedAt};

    if (!service_.trash(id)) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Delete kept napkin?"));
        box.setText(tr("This napkin is kept."));
        box.setInformativeText(
            tr("Clean up never moves a kept napkin to the trash. Deleting it now "
               "releases that protection and moves it to the trash, where it "
               "stays for %1 days.").arg(kTrashRetentionDays));
        box.setIcon(QMessageBox::Warning);
        box.addButton(QMessageBox::Cancel);
        auto* del = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != del) return;
        service_.trashConfirmed(id);
    }

    if (editingBuffer_ == id) {
        editingBuffer_ = kNoBuffer;
        canvas_->showNothingSelected();
    }
    reloadPreservingSelection();

    // Confirming the deletion of a kept buffer releases the keep, so undo has to
    // put it back — otherwise the user recovers a buffer that quietly lost the
    // protection they asked for, and the next sweep offers it up.
    const auto state = lastTrashed_;
    toast_->offer(tr("“%1” moved to trash").arg(name), [this, state] {
        if (!guarded(tr("Could not undo that"), [&] {
                service_.restore(state.id);
                if (state.kept) service_.setKept(state.id, true);
                buffers_.setModifiedAt(state.id, state.modifiedAt);
            }))
            return;
        reloadPreservingSelection();
    });
}

void MainWindow::showContextMenu(int row, const QPoint& globalPos)
{
    const BufferId id = model_->idAt(row);
    if (id == kNoBuffer) return;
    const auto buffer = buffers_.find(id);
    if (!buffer) return;

    QMenu menu(this);
    if (model_->mode() == BufferListModel::Mode::Trash) {
        menu.addAction(tr("Restore\tR"), this, [this, row] { restoreRow(row); });
        menu.addSeparator();
        menu.addAction(tr("Delete permanently\tDel"), this, [this, row] { trashRow(row); });
    } else {
        // Only actions that apply: no greyed-out rows, no giant toolbar.
        // Pin and Keep both sound like "important", so each says what it does
        // (usability test: a new user could not tell them apart).
        menu.setToolTipsVisible(true);
        auto* pin = menu.addAction(buffer->pinned ? tr("Unpin\tCtrl+P") : tr("Pin\tCtrl+P"),
                                   this, [this, row] { togglePin(row); });
        pin->setToolTip(tr("Pinned napkins stay at the top of the list."));
        auto* keep = menu.addAction(buffer->kept ? tr("Release keep\tCtrl+D") : tr("Keep\tCtrl+D"),
                                    this, [this, row] { toggleKeep(row); });
        keep->setToolTip(tr("Clean up never moves a kept napkin to the trash. "
                            "It stays until you delete it yourself."));
        menu.addAction(tr("Rename…\tF2"), this, [this, row] { renameRow(row); });
        menu.addSeparator();
        menu.addAction(tr("Delete\tDel"), this, [this, row] { trashRow(row); });
    }
    menu.exec(globalPos);
}

void MainWindow::updateEmptyState()
{
    if (model_->rowCount() > 0) { stack_->setCurrentIndex(0); return; }

    if (model_->isSearching()) {
        // The query is echoed back so you can see the typo, but truncated: a
        // pasted paragraph in the search box must not become the headline.
        QString shown = model_->query();
        if (shown.size() > 42) shown = shown.left(41) + QChar(0x2026);
        emptyState_->setContent({}, tr("Nothing matches “%1”").arg(shown),
                                tr("Search looks at your text and at your filenames."),
                                tr("Clear search"));
        stack_->setCurrentIndex(2);
        return;
    }
    if (model_->mode() == BufferListModel::Mode::Trash) {
        emptyState_->setContent(QStringLiteral(":/resources/icons/trash-empty-256.png"),
                          tr("The trash can is empty"),
                          tr("Deleted napkins and items stay here for %1 days.")
                              .arg(BufferService::trashRetentionDays()),
                          tr("Back to your napkins"));
        stack_->setCurrentIndex(2);
        return;
    }
    // Genuinely nothing yet — the one screen that has to explain the app.
    stack_->setCurrentIndex(1);
}

void MainWindow::reportProblem(const QString& title, const QString& detail)
{
    // SPEC.md §14: plain language, and the user's content is accounted for.
    QMessageBox box(QMessageBox::Warning, title, detail, QMessageBox::Ok, this);
    box.exec();
}

bool MainWindow::guarded(const QString& title, const std::function<void()>& work)
{
    try {
        work();
        return true;
    } catch (const std::exception& e) {
        reportProblem(title,
                      tr("Napkin could not complete that, and has changed nothing.\n\n%1")
                          .arg(QString::fromUtf8(e.what())));
        return false;
    }
}

// A buffer can be trashed from the list, or purged by Empty trash, while the
// canvas is still showing it. Writing to that id then violates the foreign key
// and throws — which is what crashed the application after a paste into a
// buffer that had been deleted.
bool MainWindow::currentBufferIsLive()
{
    if (editingBuffer_ == kNoBuffer) return false;

    const auto buffer = buffers_.find(editingBuffer_);
    if (buffer && !buffer->inTrash()) return true;

    editingBuffer_ = kNoBuffer;
    canvas_->showNothingSelected();
    return false;
}

bool MainWindow::addImageToCurrent(const QByteArray& bytes, const QString& mime,
                                   const QString& sourceName)
{
    if (model_->mode() != BufferListModel::Mode::Live) return false;
    if (editingBuffer_ != kNoBuffer && !currentBufferIsLive()) return false;

    const auto stored = blobs_.store(bytes, mime);
    if (!stored.ok) {
        reportProblem(tr("Could not add the image"),
                      stored.error + tr("\n\nNothing else on the napkin was changed."));
        return false;
    }

    bool appended = false;
    try {
        // The blob is already fsynced and renamed into place, so committing the
        // row now can only ever leave an orphan, never a dangling reference.
        {
            autosave_->flushNow();                       // the text lands first
            if (editingBuffer_ == kNoBuffer) {           // an empty draft gets promoted
                Draft draft;
                draft.add(Item::makeImage(stored.hash, stored.size.width(), stored.size.height(),
                                          stored.byteSize, sourceName, stored.mime,
                                          stored.animated));
                editingBuffer_ = service_.commitDraft(draft);
                if (model_->hasDraft()) {
                    model_->promoteDraft(editingBuffer_);
                } else {
                    // Pasted straight onto the stack with nothing selected:
                    // there is no draft card to promote, so the list has to
                    // learn about the new buffer the ordinary way.
                    model_->reload();
                    if (const int row = model_->rowForId(editingBuffer_); row >= 0)
                        view_->setCurrentIndex(model_->index(row, 0));
                }
            } else {
                service_.appendTo(editingBuffer_,
                    Item::makeImage(stored.hash, stored.size.width(), stored.size.height(),
                                    stored.byteSize, sourceName, stored.mime, stored.animated));
                appended = true;
            }
            canvas_->setItems(items_.listForBuffer(editingBuffer_));
            model_->invalidatePreview(editingBuffer_);
        }
    } catch (const std::exception&) {
        reportProblem(tr("Could not add the image"),
                      tr("Napkin saved the image but could not record it. "
                         "Nothing else on the napkin was changed."));
        return false;
    }

    // A napkin you just added to is the most recent one, so it moves to the
    // top of RECENT now. invalidatePreview() refreshed its time but not its
    // place, and nothing else would re-sort the list until some unrelated
    // change did.
    if (appended) reloadPreservingSelection();
    updateEmptyState();
    return true;
}

void MainWindow::completePendingCut()
{
    const PendingCut cut = pendingCut_;
    pendingCut_ = {};
    if (cut.holder == kNoBuffer) return;
    const QMimeData* now = QApplication::clipboard()->mimeData();
    if (now != cut.clip || !now || now->text() != cut.text) return;   // clipboard moved on
    const auto held = buffers_.find(cut.holder);
    if (!held || !held->inTrash()) return;                            // undone, or restored
    guarded(tr("Could not tidy up after the cut"),
            [&] { buffers_.hardDeleteEvenIfKept(cut.holder); });
    // Its Undo would put back what has just been pasted. Only its own: the
    // offers behind it are about other things and stay undoable.
    toast_->withdraw(cut.offer);
    updateEmptyTrashButton();
}

void MainWindow::pasteFromClipboard()
{
    // Pasting while looking at the trash did nothing at all. It now does what
    // Ctrl+N there does: leave the trash, then paste into a new napkin.
    if (model_->mode() != BufferListModel::Mode::Live) {
        showTrash(false);
        if (homeSegment_) homeSegment_->setChecked(true);
    }

    const auto content = readClipboard(QApplication::clipboard()->mimeData());
    // A bare return here was indistinguishable from a broken application: copy
    // a PDF in the file manager, press Ctrl+V, and Napkin did nothing and said
    // nothing. It holds text and images and nothing else (SPEC.md §3), so when
    // it cannot take what is on the clipboard it has to say which.
    if (content.kind == ClipboardContent::Kind::None) {
        toast_->inform(tr("Nothing on the clipboard that Napkin can hold — "
                          "copy some text or an image"));
        return;
    }

    // One rule for everything on the clipboard: a paste goes into the buffer
    // you are looking at, and makes a new one only when you are looking at
    // nothing. Text used to always create a buffer while images appended to the
    // current one, which meant the same gesture did two different things
    // depending on what you had copied.
    if (!currentBufferIsLive() && !model_->hasDraft()) newDraft();

    if (content.kind == ClipboardContent::Kind::Image) {
        if (addImageToCurrent(content.imageBytes, content.imageMime, QString())) completePendingCut();
        return;
    }
    if (appendTextBlock(content.text)) completePendingCut();
}

// SPEC.md §17, measured in Phase 0: **Wayland serves the clipboard only to a
// focused client** — an unfocused one reads an empty format list. And
// activateWindow() is a request to the compositor, not a synchronous change.
//
// The tray's "Paste onto a new napkin" ignored both: it raised the window, made
// a napkin, and read the clipboard in the same call stack, before any focus had
// arrived. The read came back empty, the paste returned, and all the user got
// was a blank new napkin and no explanation. Napkin's own spec had recorded the
// constraint since Phase 0; this was the one path that broke it.
void MainWindow::whenWindowIsActive(std::function<void()> then)
{
    if (isActiveWindow()) { then(); return; }

    // Polled rather than waiting on QEvent::WindowActivate: a compositor can
    // refuse the activation outright, and then that event never arrives at all
    // and the paste would be lost in silence. Give up after a second instead.
    auto* waiting = new QTimer(this);
    waiting->setInterval(25);
    auto tries = std::make_shared<int>(0);
    connect(waiting, &QTimer::timeout, this, [this, waiting, then, tries] {
        if (!isActiveWindow() && ++*tries < 40) return;
        waiting->stop();
        waiting->deleteLater();
        then();
    });
    waiting->start();
}

void MainWindow::pasteOntoNewNapkinFromTray()
{
    raiseFromOtherInstance();
    whenWindowIsActive([this] {
        // The napkin is made only once there is something to put on it. Making
        // it first is what left blank napkins behind every time the clipboard
        // held nothing Napkin could take.
        if (readClipboard(QApplication::clipboard()->mimeData()).kind
            == ClipboardContent::Kind::None) {
            toast_->inform(tr("Nothing on the clipboard that Napkin can hold — "
                              "copy some text or an image"));
            return;
        }
        // The tray item promises a *new* napkin, so it asks for one; plain
        // Ctrl+V would have appended to whichever napkin was open.
        newDraft();
        pasteFromClipboard();
    });
}

// Ctrl+T, and the path every pasted text block takes.
bool MainWindow::appendTextBlock(const QString& text)
{
    if (model_->mode() != BufferListModel::Mode::Live) return false;
    if (!currentBufferIsLive() && !model_->hasDraft()) newDraft();
    if (!flushAndReportFailure()) return false;

    if (text.isEmpty()) {          // Ctrl+T: an empty card to type into
        canvas_->addPendingTextCard();
        return false;
    }

    bool stored = false, appended = false;
    const bool ok = guarded(tr("Could not add that text"), [this, &text, &stored, &appended] {
        if (editingBuffer_ == kNoBuffer) {
            if (text.trimmed().isEmpty()) return;
            Draft draft;
            draft.setText(text);
            editingBuffer_ = service_.commitDraft(draft);
            if (editingBuffer_ == kNoBuffer) return;
            stored = true;
            if (model_->hasDraft()) model_->promoteDraft(editingBuffer_);
            else                    reloadPreservingSelection();
        } else if (!text.trimmed().isEmpty()) {
            service_.appendTo(editingBuffer_, Item::makeText(text));
            stored = appended = true;
        }
    });
    if (!ok) return false;

    if (editingBuffer_ != kNoBuffer)
        canvas_->setItems(items_.listForBuffer(editingBuffer_));
    model_->invalidatePreview(editingBuffer_);
    if (appended) reloadPreservingSelection();   // to the top of RECENT, as for images
    // Deliberately no pending card here. Pasting text produces a card; it is
    // not also a request to write another one. Ctrl+T is that request, and it
    // returns above.
    updateEmptyState();
    return stored;
}

void MainWindow::addImageFromFile()
{
    if (model_->mode() != BufferListModel::Mode::Live) return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add image"), QString(),
        formats::pickerFilter());   // built from what this build can actually open
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        reportProblem(tr("Could not read that file"),
                      tr("Napkin could not open %1.").arg(QFileInfo(path).fileName()));
        return;
    }
    if (file.size() > BlobStore::kMaxBytes) {
        reportProblem(tr("That image is too large"),
                      tr("Napkin keeps images up to %1 MB.")
                          .arg(BlobStore::kMaxBytes / (1024 * 1024)));
        return;
    }
    addImageToCurrent(file.readAll(), QString(), QFileInfo(path).fileName());
}

void MainWindow::emptyTrash()
{
    // What will actually be destroyed, not what is merely in the bin: a kept
    // row is skipped by the purge, so counting it here over-promises.
    const int count = buffers_.countPurgeable();
    if (count == 0) return;

    QMessageBox box(this);
    box.setWindowTitle(tr("Empty the trash?"));
    box.setText(count == 1 ? tr("Delete 1 napkin permanently?")
                           : tr("Delete %1 napkins permanently?").arg(count));
    box.setInformativeText(tr("This cannot be undone."));
    box.setIcon(QMessageBox::Warning);
    box.addButton(QMessageBox::Cancel);
    auto* confirm = box.addButton(tr("Delete permanently"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != confirm) return;
    emptyTrashConfirmed();
}

void MainWindow::emptyTrashConfirmed()
{
    toast_->dismiss();   // whatever it was offering no longer exists
    editingBuffer_ = kNoBuffer;
    canvas_->showNothingSelected();
    service_.emptyTrash();
    sweeper_->start();   // reclaim blobs AND thumbnails, off the UI thread
    reloadPreservingSelection();
    updateEmptyTrashButton();
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    if (toast_ && toast_->isVisible()) toast_->reposition();
}

void MainWindow::newDraft()
{
    if (!flushAndReportFailure()) return;
    // Ctrl+N while looking at the bin used to create a live buffer and display
    // it under the TRASH header.
    if (model_->mode() != BufferListModel::Mode::Live) showTrash(false);
    stack_->setCurrentIndex(0);  // leave the empty state immediately

    const int row = model_->insertDraftRow();
    editingBuffer_ = kNoBuffer;
    editingItem_   = kNoItem;
    view_->setCurrentIndex(model_->index(row, 0));
    // An empty napkin is waiting to be pasted into — or typed on. The keyboard
    // goes to the board, so "Ctrl+N, then type" (SPEC.md §18, criterion 1)
    // lands in a note instead of in the list, where letters are commands.
    canvas_->showEmptyBuffer();
    canvas_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::styleToolbarIcons()
{
    // The window's palette, not the buttons': this runs from the window's own
    // PaletteChange, before the change has reached its children, so reading
    // the button's palette drew the icon in the theme being left.
    const qreal dpr = devicePixelRatioF();
    const QPalette pal = palette();
    const QSize size = newButton_ ? newButton_->iconSize() : QSize(18, 18);
    auto apply = [&](QAbstractButton* button, const char* name, icons::GlyphPainter fallback) {
        if (!button) return;
        button->setIcon(icons::libraryIcon(name, pal, size.width(), dpr, fallback));
        button->setIconSize(size);
    };
    apply(newButton_, "plus", &icons::drawPlus);
    apply(overflowButton_, "menu", &icons::drawMenu);
    apply(settingsButton_, "settings", &icons::drawGear);
    // The switch's segments are words, as the mockup has them; the field's
    // magnifier is the one glyph that says what an empty box is for.
    if (searchGlyph_)
        searchGlyph_->setIcon(icons::libraryIcon(
            "search", pal, style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, search_), dpr,
            &icons::drawSearch));
}

// Leaving full screen goes back to how the window was — maximized stays
// maximized — rather than always to a normal window.
void MainWindow::setFullScreen(bool on)
{
    if (on == isFullScreen()) return;
    if (on) {
        wasMaximized_ = isMaximized();
        showFullScreen();
    } else if (wasMaximized_) {
        showMaximized();
    } else {
        showNormal();
    }
}

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::WindowStateChange && fullScreenAction_)
        fullScreenAction_->setChecked(isFullScreen());
    if (e->type() == QEvent::PaletteChange) styleToolbarIcons();
    if (e->type() == QEvent::FontChange || e->type() == QEvent::ApplicationFontChange)
        sizeHeaderControls();
    QMainWindow::changeEvent(e);
}

// The mockup's 38px controls and 94px segments at the default size, growing
// with the text: fixed, "Home" and "Trash" filled their segments edge to edge
// at 200% and a longer translation would have been cut (independent review).
void MainWindow::sizeHeaderControls()
{
    if (!search_) return;
    const QFontMetrics fm(font());
    const int h = std::max(tokens::kHeaderControlH, fm.height() + 20);
    search_->setFixedHeight(h);
    for (QToolButton* b : {newButton_, settingsButton_, overflowButton_})
        if (b) {
            b->setFixedSize(h, h);
            b->setIconSize(QSize(h * 18 / 38, h * 18 / 38));   // the mockup's 18 in 38
        }
    if (emptyTrashButton_) emptyTrashButton_->setFixedHeight(h);
    int segment = 94;
    for (QPushButton* b : {homeSegment_, trashToggle_}) {
        if (!b) continue;
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);   // the chosen one is drawn heavier
        segment = std::max(segment, QFontMetrics(bold).horizontalAdvance(b->text()) + 40);
    }
    for (QPushButton* b : {homeSegment_, trashToggle_})
        if (b) b->setFixedSize(segment, h);
    if (appMenu_) styleToolbarIcons();   // redrawn at the new size, not scaled up
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent*>(event)->button() == Qt::BackButton
        && model_->mode() == BufferListModel::Mode::Trash) {
        if (auto* w = qobject_cast<QWidget*>(watched); w && w->window() == this) {
            goHome();
            return true;
        }
    }
    // With the list focused on an empty napkin, typed letters were list
    // commands and type-ahead search: "P" pinned, "K" kept, anything else
    // jumped to another napkin. None of that is what someone typing on an
    // empty napkin means, so the text goes to a note instead.
    // Ctrl+V in the search box pastes onto the napkin unless the box can and
    // should take it. An image vanished — a line edit cannot hold one — and
    // text became a search for the whole pasted note, hiding every napkin
    // ("Nothing matches"). The box keeps a paste only while a query is being
    // edited; right-click ▸ Paste still puts text into an empty box.
    // Ctrl+Shift+V is the deliberate "paste into the search box". Line breaks
    // collapse to spaces: the box is one line, and a query with newlines in it
    // matches nothing a person would expect.
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_V
            && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            search_->insert(QApplication::clipboard()->text().simplified());
            return true;
        }
    }
    if (watched == search_ && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->matches(QKeySequence::Paste)) {
        const QMimeData* clip = QApplication::clipboard()->mimeData();
        const bool textOnly = clip && clip->hasText() && !clip->hasImage();
        if (!textOnly || search_->text().isEmpty()) {
            pasteFromClipboard();
            canvas_->setFocus(Qt::OtherFocusReason);   // so the next Ctrl+V pastes again
            return true;
        }
    }
    if (watched == view_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int row = view_->currentIndex().row();
        if (ItemCanvas::isTyping(key) && row >= 0
            && model_->mode() == BufferListModel::Mode::Live) {
            // Typing on a napkin in the list writes on that napkin, as typing
            // does everywhere else in Napkin.
            if (model_->idAt(row) != editingBuffer_ || !canvas_->showingANapkin()) selectBuffer(row);
            canvas_->startNote(key->text());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// Selecting a buffer in the list shows it in the canvas. There is no expand
// step any more: the pane is always there, so selection *is* opening.
void MainWindow::selectBuffer(int row)
{
    if (canvas_) canvas_->commitEditing();  // leaving a buffer commits its card
    if (!flushAndReportFailure()) return;   // do not leave the old buffer's text behind
    model_->freezeOrder(false);             // the previous buffer is done; let it re-sort

    // An abandoned draft evaporates because it never had a row (invariant 5).
    if (editingBuffer_ == kNoBuffer && model_->hasDraft() && model_->draftRow() != row) {
        const int draft = model_->draftRow();
        model_->removeDraftRow();
        if (draft >= 0 && draft < row) --row;   // the list shifted under us
    }

    const BufferId id = model_->idAt(row);
    if (row < 0) {
        editingBuffer_ = kNoBuffer;
        canvas_->showNothingSelected();
        // Deferred: this runs inside the view's own selection change (a model
        // reset clears the current row), where changing it again re-enters.
        QTimer::singleShot(0, this, &MainWindow::selectLatestIfNone);
        return;
    }

    editingBuffer_ = id;
    canvas_->setItems(id == kNoBuffer ? std::vector<Item>{} : items_.listForBuffer(id));

    // A search narrows the board as well as the list: seeing which buffer
    // matched and then having to re-find the item inside it is half an answer.
    // Found by its name alone, nothing on it matched: show all of it rather
    // than a board filtered down to nothing.
    const auto matching = id != kNoBuffer && model_->isSearching()
                              ? matchingItems(db_, id, model_->query()) : std::vector<ItemId>{};
    if (!matching.empty())
        canvas_->setSearch(model_->query(), matching);
    else
        canvas_->setSearch(QString(), {});
}

// Enter or a double-click on a buffer moves focus to its board. It does NOT
// start a text card: opening a buffer is not a request to write in it, and
// double-clicking one used to silently add a blank block.
void MainWindow::openRow(int row)
{
    if (view_->currentIndex().row() != row)
        view_->setCurrentIndex(model_->index(row, 0));
    canvas_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::removeItems(const QList<ItemId>& ids, bool cut)
{
    if (ids.isEmpty() || !currentBufferIsLive()) return;

    // Captured first: undo needs each item's original position.
    std::vector<Item> removed;
    std::vector<ItemId> removedIds;
    for (ItemId id : ids)
        if (const auto item = items_.find(id)) { removed.push_back(*item); removedIds.push_back(id); }
    if (removed.empty()) return;

    const BufferId buffer = editingBuffer_;
    const QString name = napkinName(buffer);
    const int firstRemovedIndex = canvas_->indexOf(ids.first());
    const std::optional<Buffer> before = buffers_.find(buffer);

    // Into the trash, never straight to nothing. A delete used to be recoverable
    // only while the 8-second toast was up; a new user who missed it lost the
    // item for good, while a deleted napkin sat in the trash for 30 days.
    BufferService::TrashedItems trashed;
    if (!guarded(cut ? tr("Could not cut those items") : tr("Could not delete those items"),
                 [&] { trashed = service_.trashItems(buffer, removedIds); }))
        return;

    if (trashed.wholeNapkin) {
        editingBuffer_ = kNoBuffer;
        canvas_->showNothingSelected();
        reloadPreservingSelection();
    } else {
        // Keep working where you were: land on whatever now occupies the first
        // removed slot, or the last item if you deleted off the end.
        canvas_->setItems(items_.listForBuffer(buffer), firstRemovedIndex);
        model_->invalidatePreview(buffer);
    }

    // Only a new cut replaces a pending one. A delete in between used to
    // clear it too, so the paste never finished the cut — and with offers
    // stacking, undoing everything then put back a second copy of what had
    // been cut and pasted.
    if (cut) {
        pendingCut_ = {};
        bool faithful = removed.size() == 1;
        if (!faithful) {
            faithful = true;
            for (const Item& item : removed) faithful &= item.type == ItemType::Text;
        }
        if (faithful) {
            const QMimeData* clip = QApplication::clipboard()->mimeData();
            pendingCut_ = {clip, clip ? clip->text() : QString(), trashed.holder};
        }
    }

    const int n = int(removed.size());
    // Cut is not a deletion from the user's point of view — the item is on its
    // way somewhere — so it must not announce itself as one.
    // Named, because offers stack: "Item moved to trash" twice over, from two
    // napkins, said nothing about which Undo would put back what.
    const QString message =
        cut                 ? (n == 1 ? tr("Item cut from “%1”").arg(name)
                                      : tr("%1 items cut from “%2”").arg(n).arg(name))
        : trashed.wholeNapkin ? tr("“%1” moved to trash").arg(name)
        : n == 1            ? tr("Item moved to trash from “%1”").arg(name)
                            : tr("%1 items moved to trash from “%2”").arg(n).arg(name);

    const int offer = toast_->offer(message, [this, buffer, removed, before, trashed] {
        if (!guarded(tr("Could not undo that"), [&] {
                service_.untrashItems(buffer, trashed, removed);
                if (trashed.wholeNapkin && before) {
                    if (before->kept) service_.setKept(buffer, true);
                    buffers_.setModifiedAt(buffer, before->modifiedAt);
                }
            }))
            return;
        model_->invalidatePreview(buffer);
        reloadPreservingSelection();
        if (const int row = model_->rowForId(buffer); row >= 0) {
            view_->setCurrentIndex(model_->index(row, 0));
            if (editingBuffer_ == buffer) canvas_->setItems(items_.listForBuffer(buffer), -1);
        }
    });
    if (pendingCut_.holder != kNoBuffer) pendingCut_.offer = offer;
}

void MainWindow::discardItems(const QList<ItemId>& ids)
{
    if (ids.isEmpty() || !currentBufferIsLive()) return;

    // Capture before deleting: undo has to hand the content back, not an empty
    // shell. An earlier version deleted the rows and unlinked the blobs at once,
    // so undoing within the 8-second window restored a buffer with no items and
    // rows pointing at files that were already gone.
    std::vector<Item> removed;
    for (ItemId id : ids)
        if (const auto item = items_.find(id)) removed.push_back(*item);
    if (removed.empty()) return;

    const BufferId buffer = editingBuffer_;
    const QString name = napkinName(buffer);
    const int firstRemovedIndex = canvas_->indexOf(ids.first());
    if (!guarded(tr("Could not delete those items"),
                 [&] { for (ItemId id : ids) service_.removeItem(buffer, id); }))
        return;

    // Deliberately NOT reconciling blobs here. A blob whose last reference has
    // just gone is exactly the one undo is about to need. Orphans are collected
    // by the startup sweep, which is the drift-free form anyway.
    const bool emptied = items_.countForBuffer(buffer) == 0;
    std::optional<Buffer> before;
    if (emptied) {
        before = buffers_.find(buffer);
        // An item-level delete that leaves an empty husk behind is just litter.
        guarded(tr("Could not remove the empty napkin"), [&] {
            if (!service_.trash(buffer)) service_.trashConfirmed(buffer);
        });
        editingBuffer_ = kNoBuffer;
        canvas_->showNothingSelected();
        reloadPreservingSelection();
    } else {
        // Keep working where you were: land on whatever now occupies the first
        // removed slot, or the last item if you deleted off the end.
        canvas_->setItems(items_.listForBuffer(buffer), firstRemovedIndex);
        model_->invalidatePreview(buffer);
    }

    // Hold the blobs this offer would put back, so no sweep can reclaim them
    // while it is still on screen.
    QSet<QString> holds;
    for (const auto& item : removed)
        if (!item.blobHash.isEmpty()) holds.insert(item.blobHash);

    const QString message = removed.size() == 1
        ? tr("Item deleted from “%1”").arg(name)
        : tr("%1 items deleted from “%2”").arg(removed.size()).arg(name);

    toast_->offer(emptied ? tr("“%1” moved to trash").arg(name) : message,
                  [this, buffer, removed, before, emptied] {
                      if (!guarded(tr("Could not undo that"), [&] {
                              for (const auto& item : removed) items_.restoreAt(item);
                              if (emptied) {
                                  service_.restore(buffer);
                                  if (before && before->kept) service_.setKept(buffer, true);
                                  if (before) buffers_.setModifiedAt(buffer, before->modifiedAt);
                              }
                          }))
                          return;
                      model_->invalidatePreview(buffer);
                      reloadPreservingSelection();
                      if (const int row = model_->rowForId(buffer); row >= 0)
                          view_->setCurrentIndex(model_->index(row, 0));
                  }, holds);
}

bool MainWindow::flushEditor(bool timed)
{
    if (!canvas_) return true;
    auto* editor = canvas_;
    auto dirty = editor->dirtyText();
    // Nothing new to write. A timed flush never waits for the worker; any
    // other flush does, because its caller needs what is queued to have landed.
    if (dirty.empty() && (timed || !saver_->isBusy())) return true;

    // Typing into a large note: write it on the worker. Only notes that
    // already have rows — a draft or a new note needs its id back now — and
    // only when it is worth it, or when a background save is already queued,
    // since anything else would have to wait for it anyway to stay in order.
    if (timed && saver_->isAvailable() && editingBuffer_ != kNoBuffer && !dirty.empty()) {
        bool allRows = true;
        qsizetype size = 0;
        for (const auto& d : dirty) { allRows &= d.id != kNoItem; size += d.text.size(); }
        if (allRows && (size > kBackgroundSaveChars || saver_->isBusy())) {
            std::vector<BackgroundSaver::Entry> entries;
            for (const auto& d : dirty)
                if (!isBlank(d.text)) entries.push_back({d.id, d.text, d.generation});
            if (!entries.empty()) saver_->save(editingBuffer_, std::move(entries));
            return true;
        }
    }

    // Everything else is synchronous, and lands after anything still queued:
    // an older snapshot must never be written over a newer one.
    if (saver_->isBusy()) {
        saver_->waitForIdle();
        dirty = editor->dirtyText();
    }
    if (dirty.empty()) return true;

    // Whitespace is not content: a draft of blank text still writes no row.
    bool hasContent = false;
    for (const auto& d : dirty) if (!d.text.trimmed().isEmpty()) hasContent = true;


    try {
        if (editingBuffer_ == kNoBuffer) {
            if (!hasContent) return true;            // invariant 5

            Draft draft;
            for (const auto& d : dirty)
                if (!d.text.trimmed().isEmpty()) draft.add(Item::makeText(d.text));
            editingBuffer_ = service_.commitDraft(draft);
            if (editingBuffer_ == kNoBuffer) return true;

            model_->promoteDraft(editingBuffer_);
            // Bind the composer to its new row rather than rebuilding: the user
            // may still be typing, and recreating the widgets would move the
            // caret to the start.
            const auto head = items_.listForBuffer(editingBuffer_);
            if (!head.empty() && !editor->bindComposer(head.front().id))
                editor->setItems(head);
        } else {
            for (const auto& d : dirty) {
                if (d.id != kNoItem) {
                    // An empty card is NOT removed here. Clearing a card in
                    // order to rewrite it would otherwise delete it mid-
                    // sentence and take the user's card with it. Emptiness is
                    // judged when the card is left — see editingFinished.
                    if (d.text.trimmed().isEmpty()) continue;
                    service_.updateTextItem(editingBuffer_, d.id, d.text);
                } else if (!d.text.trimmed().isEmpty()) {
                    // appendTo hands back the id, so the composer is bound by
                    // identity. Nothing else on the board is touched.
                    const ItemId created =
                        service_.appendTo(editingBuffer_, Item::makeText(d.text));
                    if (!editor->bindComposer(created)) editor->setItems(
                        items_.listForBuffer(editingBuffer_));
                    break;
                }
            }
        }
        // Editing and saved looked identical, so there was no way to tell
        // whether a change had been written. The card's own footer says so and
        // its age resets to "just now".
        QList<ItemId> saved;
        for (const auto& d : dirty)
            if (d.id != kNoItem) saved << d.id;
        editor->acknowledgeSaved(saved, nowMs());

        editor->markClean();
        model_->invalidatePreview(editingBuffer_);
        saveFailures_ = 0;
        return true;
    } catch (const std::exception&) {
        // SPEC.md §14: never silently discard content. The text stays in the
        // editor, which is why the caller must not collapse on a false.
        ++saveFailures_;
        return false;
    }
}

// Flushes, and if the write failed tells the user rather than letting the text
// evaporate. Retries are bounded: an earlier build re-armed the debounce on
// every failure and spun at ~3 transactions a second, for ever, in silence.
bool MainWindow::flushAndReportFailure(bool timed)
{
    if (flushEditor(timed)) return true;
    reportSaveFailure();
    return false;
}

void MainWindow::reportSaveFailure()
{
    if (saveFailures_ == 1 || saveFailures_ % 20 == 0) {
        reportProblem(tr("Could not save this napkin"),
                      tr("Your text is still here and has not been changed. Napkin will keep "
                         "trying.\n\nThis usually means the disk is full, or the storage "
                         "folder is not writable."));
    }
    if (saveFailures_ < 60) autosave_->noteChange();   // bounded retry
}

void MainWindow::openImageItem(ItemId id)
{
    const auto item = items_.find(id);
    if (!item || item->type != ItemType::Image) return;

    const QString path = blobs_.pathFor(item->blobHash, item->mime);
    if (!QFile::exists(path)) {
        reportProblem(tr("The image is missing"),
                      tr("Napkin can no longer find the file for this image. "
                         "The rest of the napkin is unchanged."));
        return;
    }
    // Every image on the board, in the order the board shows them, so the
    // arrow keys page through the napkin from the one that was opened.
    std::vector<Lightbox::Image> images;
    int start = 0;
    for (ItemId other : canvas_->itemOrder()) {
        const auto each = other == id ? item : items_.find(other);
        if (!each || each->type != ItemType::Image) continue;
        if (other == id) start = int(images.size());
        images.push_back({blobs_.pathFor(each->blobHash, each->mime), each->animated,
                          each->sourceName});
    }
    if (images.empty()) images.push_back({path, item->animated, item->sourceName});
    Lightbox box(std::move(images), start, this);
    lightbox_ = &box;
    box.exec();
    lightbox_ = nullptr;
}

bool MainWindow::event(QEvent* e)
{
    // Losing the window is one of the moments where waiting would be
    // indefensible (SPEC.md §8).
    if (e->type() == QEvent::WindowDeactivate) {
        autosave_->flushNow();
        model_->freezeOrder(false);
    }
    return QMainWindow::event(e);
}

// Quitting cannot be spelled close(), which is how it was spelled everywhere.
//
// Two separate reasons, and between them every route out of Napkin was broken
// while the tray setting was on:
//
//   - close() ends the process only as a side effect of quitOnLastWindowClosed,
//     and that never fires for a window that is ALREADY HIDDEN — measured, not
//     assumed. The tray's Quit is used precisely when the window has been
//     closed to the tray, so it did nothing at all.
//   - closeEvent() deliberately hides to the tray instead of closing, so
//     File ▸ Quit merely hid the window.
//
// close() is still how we get there, because it is what flushes unsaved text
// and what can refuse; the quit is then explicit rather than incidental.
bool MainWindow::quitNapkin()
{
    reallyQuitting_ = true;
    if (!close()) {          // unsaved text that could not be written
        reallyQuitting_ = false;
        return false;
    }
    emit quitting();
    QCoreApplication::quit();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    // Closing with unsaved text that cannot be written would destroy it with no
    // trace at all, which is the worst version of this failure.
    if (!flushAndReportFailure()) { e->ignore(); return; }

    // Hide to the tray rather than quit — but only to a tray that is actually
    // showing. The setting alone is not enough: a desktop can have no tray, or
    // the icon can have failed to appear, and hiding the only window to a place
    // that does not exist leaves no way back into the application.
    if (!reallyQuitting_ && SettingsDialog::keepInTray() && tray_ && tray_->isShowing()) {
        hide();
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

// Creating the icon is what makes it appear, so it is created on demand and
// destroyed when the setting is turned off rather than being left hidden.
void MainWindow::applyShortcutSetting()
{
    const bool wanted = SettingsDialog::captureShortcut();
    if (!wanted) {
        if (shortcut_) shortcut_->disable();
        return;
    }
    if (!shortcut_) {
        shortcut_ = new GlobalShortcut(this);
        connect(shortcut_, &GlobalShortcut::activated, this, &MainWindow::pasteFromGlobalShortcut);
        connect(shortcut_, &GlobalShortcut::stateChanged, this, [this](const QString& problem) {
            if (!problem.isEmpty()) toast_->inform(problem);
            else if (shortcut_->isActive() && !shortcut_->trigger().isEmpty())
                toast_->inform(tr("%1 now pastes into Napkin from any app")
                                   .arg(shortcut_->trigger()));
        });
    }
    shortcut_->enable();
}

// A small window of its own, made fresh for each press, rather than raising
// the main one. Measured on KDE Plasma 6.7 / Wayland: the portal's Activated
// signal carries no activation token, so KWin refuses to raise an existing
// window — it only flashes the taskbar entry — and an unfocused window is
// served an empty clipboard. A NEW window from the same running process is
// given focus (46 ms) and can read the clipboard. It closes itself when done,
// and focus goes back to whatever the user was in; the main window never moves.
void MainWindow::pasteFromGlobalShortcut()
{
    auto* capture = new QLabel(tr("Adding to Napkin…"));
    capture->setObjectName(QStringLiteral("captureWindow"));
    capture->setAttribute(Qt::WA_DeleteOnClose);
    capture->setWindowTitle(tr("Napkin"));
    capture->setAlignment(Qt::AlignCenter);
    capture->setMargin(24);
    capture->setMinimumWidth(360);
    capture->setWindowIcon(windowIcon());
    capture->show();
    capture->raise();
    capture->activateWindow();
    capture_ = capture;

    auto* waiting = new QTimer(capture);
    waiting->setInterval(20);
    auto tries = std::make_shared<int>(0);
    connect(waiting, &QTimer::timeout, capture, [this, capture, waiting, tries] {
        if (!capture->isActiveWindow() && ++*tries < 50) return;
        waiting->stop();
        QString said;
        if (!capture->isActiveWindow()) {
            // Refused focus after all: say so where it can be seen, rather than
            // pasting an empty clipboard and calling it done.
            said = tr("Napkin could not read the clipboard from here. Switch to Napkin "
                      "and press Ctrl+V.");
        } else if (readClipboard(QApplication::clipboard()->mimeData()).kind
                   == ClipboardContent::Kind::None) {
            said = tr("Nothing on the clipboard that Napkin can hold — copy some text or an image");
        } else {
            pasteFromClipboard();
            said = currentBufferIsLive() ? tr("Added to “%1”").arg(napkinName(editingBuffer_))
                                         : tr("Added to Napkin");
        }
        capture->setText(said);
        QTimer::singleShot(said.size() > 60 ? 2600 : 1100, capture, &QWidget::close);
    });
    waiting->start();
}

void MainWindow::applyTraySetting()
{
    const bool wanted = SettingsDialog::keepInTray();
    if (!wanted) {
        delete tray_;
        tray_ = nullptr;
        return;
    }
    if (tray_) return;

    tray_ = new TrayIcon(this);
    connect(tray_, &TrayIcon::showRequested, this, &MainWindow::raiseFromOtherInstance);
    connect(tray_, &TrayIcon::newBufferRequested, this, [this] {
        raiseFromOtherInstance();
        newDraft();
    });
    connect(tray_, &TrayIcon::pasteRequested, this, &MainWindow::pasteOntoNewNapkinFromTray);
    connect(tray_, &TrayIcon::quitRequested, this, [this] { quitNapkin(); });
    tray_->setVisible(true);
}

void MainWindow::raiseFromOtherInstance()
{
    showNormal();
    raise();
    activateWindow();
}

}  // namespace napkin
