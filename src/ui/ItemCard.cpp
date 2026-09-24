#include "ItemCard.h"
#include "CardFooter.h"
#include "LinkChip.h"
#include "../domain/Calc.h"
#include "../domain/Links.h"
#include "MatchHighlighter.h"
#include "Tokens.h"
#include "../domain/Clock.h"
#include "../domain/Preview.h"
#include "../domain/TimeFormat.h"
#include "../media/BlobStore.h"
#include "../media/ClipboardContent.h"
#include "../media/ImageFormats.h"
#include "../media/Thumbnailer.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolTip>
#include <QVBoxLayout>
#include <functional>

namespace napkin {

void ItemCard::refreshTimestamp()
{
    if (footer_) footer_->refreshTimestamp();
}
namespace {

using namespace tokens;

// "image/png" -> "PNG", for a pasted image with no filename of its own.
QString formatLabel(const QString& mime)
{
    const int slash = mime.indexOf(QLatin1Char('/'));
    QString suffix = slash >= 0 ? mime.mid(slash + 1) : mime;
    if (suffix.startsWith(QLatin1String("svg"))) suffix = QStringLiteral("svg");
    return suffix.toUpper();
}

// QPlainTextEdit would otherwise drop an image on the floor and paste whatever
// text came alongside it — the exact inversion of the §4 preference order.
class PasteAwareTextEdit : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<bool(const QMimeData*)> onPaste;
    // Adds the card's own entries to the editor's menu while editing.
    std::function<void(QMenu*)> extendMenu;

protected:
    void contextMenuEvent(QContextMenuEvent* e) override
    {
        if (isReadOnly() || !extendMenu) { QPlainTextEdit::contextMenuEvent(e); return; }
        // A right-click outside the selection moves the caret there first, as
        // in most editors, so Calculate acts on the line that was clicked —
        // not on wherever the caret was left (independent review).
        const QTextCursor clicked = cursorForPosition(e->pos());
        const QTextCursor current = textCursor();
        if (!current.hasSelection() || clicked.position() < current.selectionStart()
            || clicked.position() > current.selectionEnd())
            setTextCursor(clicked);
        // What QPlainTextEdit does itself, plus our entries.
        QMenu* menu = createStandardContextMenu(e->pos());
        menu->setAttribute(Qt::WA_DeleteOnClose);
        extendMenu(menu);
        menu->popup(e->globalPos());
    }
    bool canInsertFromMimeData(const QMimeData* source) const override
    {
        return (source && source->hasImage()) || QPlainTextEdit::canInsertFromMimeData(source);
    }
    void insertFromMimeData(const QMimeData* source) override
    {
        if (onPaste && onPaste(source)) return;
        QPlainTextEdit::insertFromMimeData(source);
    }
};

}  // namespace

// --- base --------------------------------------------------------------------

ItemCard::ItemCard(const Item& item, QWidget* parent) : QWidget(parent), item_(item)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
}

void ItemCard::setFooterAction(const QString& label)
{
    if (footer_) footer_->setActionLabel(label);
}

void ItemCard::setContent(QWidget* content, const QString& copyLabel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kCardPad, kCardPad, kCardPad, kCardPad - 6);
    layout->setSpacing(kGapTight);
    layout->addWidget(content, 1);

    // Every card gets one, the composer included. Without it a card made with
    // Ctrl+T was visibly a different kind of object from every other card —
    // no copy action, no age — until it happened to be saved.
    footer_ = new CardFooter(copyLabel, this);
    footer_->setTimestamp(item_.modifiedAt ? item_.modifiedAt : item_.createdAt);
    connect(footer_, &CardFooter::actionTriggered, this,
            [this] { emit copyRequested(item_.id); });
    layout->addWidget(footer_);
}

void ItemCard::setSelected(bool selected)
{
    if (selected_ == selected) return;
    selected_ = selected;
    update();
}

void ItemCard::acknowledge(const QString& message)
{
    if (footer_) footer_->acknowledgeAction(message);
}

void ItemCard::noteSaved(Timestamp when)
{
    item_.modifiedAt = when;
    if (footer_) {
        footer_->setTimestamp(when);
        footer_->flash(tr("Saved"));
    }
}

// Palettes captured at construction go stale the moment the desktop theme
// changes: card text stayed the old colour until the buffer was reopened.
void ItemCard::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        applyPalette();
    QWidget::changeEvent(e);
}

void ItemCard::setClipped(bool clipped)
{
    clipped_ = clipped;
    if (footer_) footer_->setClipped(clipped);
}

void ItemCard::setCurrent(bool current)
{
    if (current_ == current) return;
    current_ = current;
    update();
}

int ItemCard::chromeHeight() const
{
    return footer_ ? cardChromeHeight(font()) : kCardPad * 2 - 6;
}

int ItemCard::heightForColumn(int width) const
{
    const int inner = std::max(40, width - kCardPad * 2);
    const int natural = contentHeightForWidth(inner) + chromeHeight();

    clipped_ = natural > kCardMaxHeight;
    if (footer_) footer_->setClipped(clipped_);
    return std::clamp(natural, kCardMinHeight, kCardMaxHeight);
}

void ItemCard::mousePressEvent(QMouseEvent* e)
{
    emit selectRequested(item_.id, e->modifiers());
    // Accept it. QWidget::mousePressEvent ignores the event, which would let it
    // bubble up to the canvas — whose handler clears the selection this click
    // just made.
    e->accept();
}

void ItemCard::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette& pal = palette();
    const bool editing = hasEditFocus();

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(box, kCardRadius, kCardRadius);

    // A card is a piece of paper on the desk: its own surface, its own edge.
    // An earlier build drew text cards with no border and no fill at all, which
    // made the board read as loose text rather than as things you can pick up.
    p.fillPath(path, pal.color(QPalette::Base));

    // Selection tints the card; EDITING deliberately does not. A wash behind
    // text you are actively reading and typing makes it hard to read, and the
    // border plus the caret are two signals already.
    if (selected_ && !editing)
        p.fillPath(path, highlight(pal, isLightTheme(pal) ? kFillSelectedLight
                                                          : kFillSelectedDark));

    // The accent is darkened until it actually meets 3:1 against the card. The
    // raw Highlight at alpha 160 measured 1.74:1 in Breeze Light — FAINTER than
    // the 3.10:1 resting border it replaced, so selecting a card made its edge
    // harder to see and the state ended up carried by hue alone.
    const QColor border = selected_ || editing
        ? readableAccent(pal, editing ? 1.0 : 0.82)
        : text(pal, cardBorderAlpha(pal, hovered_));
    p.setPen(QPen(border, selected_ || editing ? 2.0 : 1.0));
    p.drawPath(path);

    // Keyboard focus is its own signal, drawn inside the border so it never
    // collides with it. Without this a focused card was pixel-identical to its
    // neighbours and the board could not be operated by keyboard at all.
    //
    // Only when it is adding something, though. Clicking a card makes it both
    // current and selected, so the common case drew a solid accent edge with a
    // dotted accent ring 1px inside it — two near-coincident strokes that read
    // as a rendering artefact rather than as one confident selection. The
    // ring's job is to show where the keyboard is when the selection is not
    // already saying so.
    // A card that has the keyboard itself (Tab lands on cards) is current too.
    if ((current_ || hasFocus()) && !selected_ && !editing) {
        QPainterPath ring;
        ring.addRoundedRect(box.adjusted(3, 3, -3, -3), kCardRadius - 3, kCardRadius - 3);
        QPen focusPen(readableAccent(pal, 1.0), 2.0, Qt::DotLine);
        p.setPen(focusPen);
        p.drawPath(ring);
    }

}

void ItemCard::focusInEvent(QFocusEvent* e)  { QWidget::focusInEvent(e);  update(); }
void ItemCard::focusOutEvent(QFocusEvent* e) { QWidget::focusOutEvent(e); update(); }

void ItemCard::enterEvent(QEnterEvent* e)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(e);
}

void ItemCard::leaveEvent(QEvent* e)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(e);
}

// --- text --------------------------------------------------------------------

TextItemCard::TextItemCard(const Item& item, QWidget* parent) : ItemCard(item, parent)
{
    auto* edit = new PasteAwareTextEdit;
    edit->setPlainText(item.text);
    // Start at the beginning, not the end. moveCursor(End) here left the
    // viewport scrolled — horizontally as well as vertically — so the first few
    // pixels of every line were clipped off the left edge. A card is read from
    // the top anyway; the caret only needs to be at the end when you are about
    // to append to it.
    edit->moveCursor(QTextCursor::Start);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setPlaceholderText(tr("Write something…"));
    edit->setTabChangesFocus(true);
    edit->viewport()->setAutoFillBackground(false);
    // Transparency through the palette, NOT a stylesheet.
    //
    // A stylesheet makes Qt set an explicitly-resolved palette on the widget,
    // and the widget then stops following application palette changes. Switching
    // theme left every card's text at the old theme's colour — black on a dark
    // card — until something rebuilt the card, which is why clicking another
    // buffer appeared to "fix" it. applyPalette() below re-derives this on every
    // palette change, which is the same contract every other card colour has.
    edit->setAttribute(Qt::WA_StyledBackground, false);
    edit->document()->setDocumentMargin(1);
    // Read-only until you ask to edit, so a click lands on the card rather than
    // in the text. NoFocus while read-only matters for more than tab order: the
    // caret blink only starts on a focus-IN event, and the editor was already
    // holding focus before editing began — so enabling the caret produced no
    // focus change, no blink, and no visible cursor until an arrow key forced a
    // repaint. Giving up focus while read-only guarantees a real focus-in.
    edit->setReadOnly(true);
    edit->setTextInteractionFlags(Qt::NoTextInteraction);
    edit->setFocusPolicy(Qt::NoFocus);
    edit->onPaste = [this](const QMimeData* source) {
        const auto content = readClipboard(source);
        if (content.kind != ClipboardContent::Kind::Image) return false;
        emit imagePasted(content.imageBytes, content.imageMime);
        return true;
    };
    // The button for Ctrl+Tab, where a mouse user will look for it: next to
    // the Cut, Copy and Paste that act on the same text.
    edit->extendMenu = [this](QMenu* menu) {
        menu->addSeparator();
        menu->addAction(tr("Calculate\tCtrl+Tab"), this, [this] { calculate(); })
            ->setObjectName(QStringLiteral("calculateAction"));
    };
    edit_ = edit;

    // Belt and braces: with the horizontal bar off, a stray scroll offset is
    // invisible but still clips the text.
    edit_->horizontalScrollBar()->setValue(0);
    edit_->verticalScrollBar()->setValue(0);

    // The chip and the editor share the content slot: exactly one is visible.
    // A stack rather than a swap, so switching between them costs no rebuild
    // and the card keeps its identity across edits.
    auto* content = new QWidget;
    auto* slot = new QVBoxLayout(content);
    slot->setContentsMargins(0, 0, 0, 0);
    slot->setSpacing(0);
    chip_ = new LinkChip;
    chip_->hide();
    connect(chip_, &LinkChip::openRequested, this, &TextItemCard::openUrlRequested);
    slot->addWidget(chip_);
    slot->addWidget(edit_, 1);

    setContent(content, tr("Copy text"));

    lastText_ = edit_->toPlainText();
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
        // textChanged also fires for formatting-only changes, and the search
        // highlighter reformats the whole document — which marked every visible
        // card dirty, autosaved it, and flashed "Saved" on cards nobody had
        // touched. Only a change to the actual characters is an edit.
        const QString now = edit_->toPlainText();
        if (now == lastText_) return;
        lastText_ = now;

        dirty_ = true;
        updateAccessibleName();
        refreshChip(hasEditFocus());
        emit edited();
        emit heightChanged();
    });
    updateAccessibleName();
    refreshChip(/*editing=*/false);

    // Created for every text card, not lazily on first search: URLs in prose
    // are marked all the time, so there is always something to highlight.
    highlighter_ = new MatchHighlighter(edit_->document());

    applyPalette();   // the editor starts transparent, not merely becomes it
    edit_->installEventFilter(this);
    edit_->viewport()->installEventFilter(this);
}

QString TextItemCard::text() const { return edit_->toPlainText(); }

void TextItemCard::setSearchTerms(const QStringList& terms)
{
    highlighter_->setTerms(terms);
}

// The URL under a point, or empty. Used for Ctrl+click: a plain click has to go
// on meaning "select this card", because that is what a click means on every
// other card, and a link in the middle of a paragraph is not a reason to make
// one card behave differently from its neighbours.
QString TextItemCard::urlAt(const QPoint& viewportPos) const
{
    const QTextCursor cursor = edit_->cursorForPosition(viewportPos);
    const QString block = cursor.block().text();
    const int offset = cursor.positionInBlock();
    for (const auto& span : links::findUrls(block, 64))
        if (offset >= span.start && offset <= span.start + span.length)
            return span.url;
    return {};
}

// A screen reader gets the same thing a sighted user does: the first line, and
// when it happened. Six of nine cards announced nothing at all before this.
void TextItemCard::updateAccessibleName()
{
    const QString body = firstLine(edit_->toPlainText());
    setAccessibleName(body.isEmpty() ? tr("Empty note") : body.left(80));
    const Timestamp when = item_.modifiedAt ? item_.modifiedAt : item_.createdAt;
    setAccessibleDescription(when ? tr("Note, %1").arg(relativeTime(when, nowMs()))
                                  : tr("New note"));
}
bool TextItemCard::textHasFocus() const { return edit_->hasFocus(); }

// Editing MODE, not window focus: a card being edited must still look edited
// when the window is inactive, and window focus is not something a headless
// test can grant.
bool TextItemCard::hasEditFocus() const
{
    return edit_->textInteractionFlags() & Qt::TextEditorInteraction;
}

void TextItemCard::focusTextInteraction()
{
    edit_->setFocusPolicy(Qt::StrongFocus);
    edit_->setReadOnly(false);
    edit_->setTextInteractionFlags(Qt::TextEditorInteraction);
    edit_->setCursorWidth(2);
}

void TextItemCard::focusText()
{
    focusTextInteraction();
    edit_->setFocus(Qt::OtherFocusReason);
    edit_->moveCursor(QTextCursor::End);
}

void TextItemCard::beginEditing(bool moveToEnd)
{
    if (hasEditFocus()) return;
    focusTextInteraction();
    refreshChip(/*editing=*/true);   // out of the way before the caret arrives
    edit_->setFocus(Qt::MouseFocusReason);
    if (moveToEnd) edit_->moveCursor(QTextCursor::End);
    emit editingStarted(itemId());
    update();
}

// Leaving edit mode. This — not every autosave flush — is where an empty card
// is judged: a user clearing a card to rewrite it must not have it deleted out
// from under them mid-sentence.
void TextItemCard::endEditing()
{
    if (!hasEditFocus()) return;
    edit_->setReadOnly(true);
    edit_->setTextInteractionFlags(Qt::NoTextInteraction);
    edit_->setFocusPolicy(Qt::NoFocus);
    edit_->clearFocus();
    refreshChip(/*editing=*/false);   // what you typed may now be a link, or not
    update();
    emit heightChanged();
    emit editingFinished(itemId(), text().trimmed().isEmpty());
}

// Everything this card derived from the palette, re-derived. The editor is the
// important one: it holds the user's own words, and it is the widget a stale
// palette makes unreadable rather than merely wrong.
void TextItemCard::applyPalette()
{
    QPalette pal = QGuiApplication::palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Window, Qt::transparent);
    edit_->setPalette(pal);
    edit_->viewport()->setPalette(pal);

    // The highlighter picks its colours when it runs, so marks laid down under
    // the old theme keep the old theme's accent until it runs again.
    if (highlighter_) highlighter_->rehighlight();
}

QString TextItemCard::linkUrl() const
{
    return links::soleUrl(edit_->toPlainText()).value_or(QString());
}

bool TextItemCard::showingChip() const
{
    return chipShown_;
}

// A chip is how the card is *drawn*, so the one thing that must never happen is
// the chip standing between you and the text. It steps aside for the whole of
// editing and comes back when editing ends — including when what you typed has
// stopped being a URL, or has just become one.
void TextItemCard::refreshChip(bool editing)
{
    if (!chip_) return;
    const QString url = editing ? QString() : linkUrl();
    const bool show = !url.isEmpty();

    if (show) chip_->setUrl(url);
    chipShown_ = show;
    chip_->setVisible(show);
    edit_->setVisible(!show);
    setFooterAction(show ? tr("Copy link") : tr("Copy text"));
}

int TextItemCard::contentHeightForWidth(int innerWidth) const
{
    // A chip is a fixed object; it does not wrap and does not grow.
    if (showingChip()) return LinkChip::preferredHeight();

    // Measured against a document of our own, never the editor's.
    //
    // QPlainTextEdit uses QPlainTextDocumentLayout, which ignores setTextWidth
    // and wraps to the *viewport's* current width instead — and reports its
    // height in LINES rather than pixels. A card measured at construction, when
    // the viewport has no width yet, therefore came back as one line. That is
    // why a freshly pasted paragraph appeared as a single scrollable line.
    //
    // A plain QTextDocument honours setTextWidth and answers in pixels, so it
    // gives the right height before the widget has ever been shown.
    if (!measure_) {
        measure_ = new QTextDocument(const_cast<TextItemCard*>(this));
        measure_->setDocumentMargin(1);   // match the editor's, or heights drift
    }
    const QString current = edit_->toPlainText();
    if (current != measured_) {
        measure_->setDefaultFont(edit_->font());
        measure_->setPlainText(current.isEmpty() ? edit_->placeholderText() : current);
        measured_ = current;
    }
    measure_->setTextWidth(innerWidth);
    return int(std::ceil(measure_->size().height())) + 2;
}

void TextItemCard::mouseDoubleClickEvent(QMouseEvent* e)
{
    // Double-clicked the card's padding rather than the text itself.
    beginEditing();
    edit_->moveCursor(QTextCursor::End);
    e->accept();
}

void TextItemCard::insertText(const QString& text)
{
    focusText();
    edit_->insertPlainText(text);
}

// SPEC.md §7 "Calculating a line". Only ever on request: nothing is
// evaluated, and nothing in the note changes, until Ctrl+Tab or Calculate.
bool TextItemCard::calculate()
{
    if (!hasEditFocus()) return false;
    QTextDocument* doc = edit_->document();
    const QTextCursor caret = edit_->textCursor();

    // Replaces part of one line. Returns false when the answer is already
    // there, so recalculating an unchanged line adds nothing to undo.
    const auto apply = [doc](const QTextBlock& block, const calc::LineEdit& change) {
        QTextCursor c(doc);
        c.setPosition(block.position() + change.start);
        c.setPosition(block.position() + change.start + change.length, QTextCursor::KeepAnchor);
        if (c.selectedText() == change.text) return false;
        c.insertText(change.text);
        return true;
    };

    calc::Error error = calc::Error::NotAnExpression;
    bool answered = false;   // there was an answer to give, new or not
    bool changed = false;    // and the text is now different
    // One edit block, so one Ctrl+Z takes back the whole calculation — and so
    // the answer is not merged into the typing before it, which would make
    // that Ctrl+Z take the sum away too.
    QTextCursor block(doc);
    block.beginEditBlock();
    QTextCursor after;   // where the caret goes when an answer was written

    if (caret.hasSelection()) {
        const QTextBlock first = doc->findBlock(caret.selectionStart());
        QTextBlock last = doc->findBlock(caret.selectionEnd());
        // Shift+Down from the start of a line selects up to the start of the
        // next one, which is not a request to calculate that line too.
        if (last != first && caret.selectionEnd() == last.position()) last = last.previous();
        if (first == last && doc->findBlock(caret.selectionEnd()) == first) {
            // Exactly what was selected, whatever surrounds it.
            const calc::LineResult r = calc::calculateSelection(
                first.text(), caret.selectionStart() - first.position(),
                caret.selectionEnd() - first.position());
            if (r.edit) {
                changed = apply(first, *r.edit);
                after = QTextCursor(doc);
                after.setPosition(first.position() + r.edit->start + int(r.edit->text.size()));
                answered = true;
            } else {
                error = r.error;
            }
        } else {
            // Every line in the selection that asks for an answer.
            for (QTextBlock b = first; b.isValid(); b = b.next()) {
                const calc::LineResult r = calc::calculateLine(b.text(), calc::Scope::MarkedOnly);
                if (r.edit) {
                    changed = apply(b, *r.edit) || changed;
                    answered = true;
                } else if (r.error != calc::Error::NotAnExpression) {
                    error = r.error;
                }
                if (b == last) break;
            }
        }
    } else {
        const QTextBlock line = caret.block();
        const calc::LineResult r = calc::calculateLine(line.text(), calc::Scope::CaretLine);
        if (r.edit) {
            changed = apply(line, *r.edit);
            after = QTextCursor(line);
            after.movePosition(QTextCursor::EndOfBlock);
            answered = true;
        } else {
            error = r.error;
        }
    }
    // Typing straight after the answer would otherwise be merged into it:
    // QTextDocument joins a lone insert onto an edit block that ended with an
    // insert, so Ctrl+Z after a typo took the answer too (independent review).
    // A no-op custom step, last in the block, is Qt's own way to end that.
    struct UndoBarrier : QAbstractUndoItem {
        void undo() override {}
        void redo() override {}
    };
    if (changed) doc->appendUndoItem(new UndoBarrier);
    block.endEditBlock();
    if (!after.isNull()) edit_->setTextCursor(after);

    if (!answered) {
        // Said at the caret rather than beeped: a silent Ctrl+Tab reads as a
        // key that does nothing, and a line of prose is the usual reason.
        QString why;
        switch (error) {
        case calc::Error::DivideByZero: why = tr("Can’t divide by zero"); break;
        case calc::Error::Undefined:    why = tr("That has no answer"); break;
        default:
            why = caret.hasSelection() ? tr("Nothing to calculate in the selection")
                                       : tr("Nothing to calculate on this line");
        }
        const QRect at = edit_->cursorRect();
        QToolTip::showText(edit_->viewport()->mapToGlobal(at.bottomLeft()), why, edit_);
    }
    return answered;
}

void TextItemCard::selectAllText()
{
    edit_->selectAll();
}

bool TextItemCard::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        // Already editing: a double-click means what it means in any editor —
        // select the word. Intercepting it here placed a caret instead, so
        // double-clicking "Tuesday" to replace it produced "TuesWednesdayday"
        // (usability test, 2026-09-19).
        if (hasEditFocus()) return false;
        // Enable interaction, then place the caret ourselves from the click
        // position. Letting the editor handle the event did not work: the press
        // that began the double-click arrived while the widget was still
        // read-only, so the editor had no cursor to move and none appeared
        // until an arrow key was pressed.
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint pos = edit_->viewport()->mapFrom(
            qobject_cast<QWidget*>(watched), mouse->position().toPoint());
        beginEditing(/*moveToEnd=*/false);
        // Select the word that was double-clicked, as an editor would. This
        // once deliberately placed a bare caret, reasoning that a selection
        // hides it; but the highlighted word says "editable" just as plainly,
        // and replacing a word is what a double-click on one is for.
        QTextCursor cursor = edit_->cursorForPosition(pos);
        cursor.select(QTextCursor::WordUnderCursor);
        edit_->setTextCursor(cursor);
        edit_->ensureCursorVisible();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);

        // Ctrl+click follows a link in prose, editing or not. Ctrl is also the
        // selection modifier on a card, so this is checked first and only wins
        // when the click actually landed on a URL — clicking Ctrl anywhere else
        // in the text still extends the selection.
        if (mouse->button() == Qt::LeftButton
            && (mouse->modifiers() & Qt::ControlModifier)) {
            const QPoint pos = edit_->viewport()->mapFrom(
                qobject_cast<QWidget*>(watched), mouse->position().toPoint());
            const QString url = urlAt(pos);
            if (!url.isEmpty()) {
                emit openUrlRequested(url);
                return true;
            }
        }

        // While read-only the press means "select me"; once editing, it belongs
        // to the caret.
        if (!hasEditFocus()) {
            emit selectRequested(itemId(), mouse->modifiers());
            return true;
        }
    }
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;

        // Ctrl+Tab calculates. Plain Tab is taken: it walks between cards, and
        // that is how the board is reached without a mouse.
        if (key->key() == Qt::Key_Tab && (key->modifiers() & Qt::ControlModifier)
            && hasEditFocus()) {
            calculate();
            return true;
        }

        // Ctrl+Enter commits and leaves. Plain Enter belongs to the text — a
        // note is several lines more often than it is one.
        if (enter && (key->modifiers() & Qt::ControlModifier)) {
            endEditing();
            emit escaped();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            endEditing();
            emit escaped();
            return true;
        }
    }
    return ItemCard::eventFilter(watched, event);
}

// --- image -------------------------------------------------------------------

ImageItemCard::ImageItemCard(const Item& item, Thumbnailer& thumbs, BlobStore& blobs,
                             QWidget* parent)
    : ItemCard(item, parent)
{
    setToolTip(tr("Double-click to view full size"));

    auto* holder = new QWidget;
    auto* layout = new QVBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    view_ = new QLabel;
    view_->setAlignment(Qt::AlignCenter);

    // Through the Thumbnailer, which caches to disk and negative-caches
    // failures. The card used to decode the original blob itself, on the UI
    // thread, in its constructor: Qt's PNG handler ignores setScaledSize and
    // decodes in full, so a 208 KB 8000x8000 screenshot cost 288 MB and 410 ms
    // — per card, every time the buffer was opened. The Thumbnailer pays that
    // once, then reads a small cached file.
    const bool fileExists = QFile::exists(blobs.pathFor(item.blobHash, item.mime));
    if (fileExists)
        source_ = thumbs.forBlob(item.blobHash, item.mime, kCardMaxWidth * 2);

    if (source_.isNull()) {
        // Two different failures, and telling them apart matters: one means the
        // file is gone, the other means it is right there and unreadable. The
        // card used to claim "no longer on disk" for both.
        view_->setText(fileExists ? tr("This image is too large to display.")
                                  : tr("This image is no longer on disk."));
        view_->setEnabled(false);
    }
    layout->addWidget(view_);

    QStringList facts;
    facts << (item.sourceName.isEmpty() ? formatLabel(item.mime) : item.sourceName);
    if (item.width > 0 && item.height > 0)
        facts << QStringLiteral("%1 × %2").arg(item.width).arg(item.height);
    if (item.byteSize > 0) facts << formatBytes(item.byteSize);
    if (item.animated) facts << tr("animated");

    caption_ = new QLabel(facts.join(QStringLiteral(" · ")));
    caption_->setFont(scaledBy(caption_->font(), kTypeCaption));
    QPalette capPal = caption_->palette();
    capPal.setColor(QPalette::WindowText, text(palette(), kTextTertiary));
    caption_->setPalette(capPal);
    layout->addWidget(caption_);

    setContent(holder, tr("Copy image"));
    setAccessibleName(item.sourceName.isEmpty() ? tr("Image") : item.sourceName);
    setAccessibleDescription(facts.join(QStringLiteral(", ")));
}

QString ImageItemCard::asPlainText() const
{
    return item_.sourceName.isEmpty() ? tr("[image]") : item_.sourceName;
}

int ImageItemCard::contentHeightForWidth(int innerWidth) const
{
    const int captionH = caption_ ? caption_->sizeHint().height() + 6 : 0;
    if (source_.isNull()) return 96 + captionH;

    // Never upscaled: a 200x140 favicon draws at 200x140. Stretching a small
    // image to fill a column is the fastest way to make a UI look cheap.
    const int drawn = source_.height() * std::min(innerWidth, source_.width())
                      / std::max(1, source_.width());
    return drawn + captionH;
}

void ImageItemCard::applyPalette()
{
    if (!caption_) return;
    QPalette pal = caption_->palette();
    pal.setColor(QPalette::WindowText, text(palette(), kTextTertiary));
    caption_->setPalette(pal);
    rescale();   // the image's edge is drawn in the theme's colour
}

void ImageItemCard::rescale()
{
    if (source_.isNull()) return;
    // Must use the same chrome arithmetic as heightForColumn, or the caption
    // ends up painted over the bottom of the picture.
    const int available = std::max(60, width() - kCardPad * 2);
    const int captionH = caption_ ? caption_->sizeHint().height() + 6 : 0;
    const int room = std::max(60, height() - chromeHeight() - captionH);

    const QSize target = source_.size().scaled(available, room, Qt::KeepAspectRatio)
                             .boundedTo(source_.size());
    QPixmap shown = source_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // The same hairline edge the list's thumbnails carry, for the same reason:
    // a picture the colour of the card read as empty space.
    {
        QPainter edge(&shown);
        QColor c = palette().color(QPalette::Text);
        c.setAlpha(60);
        edge.setPen(QPen(c, 1));
        edge.drawRect(QRectF(shown.rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    view_->setPixmap(shown);
}

void ImageItemCard::resizeEvent(QResizeEvent* e)
{
    ItemCard::resizeEvent(e);
    rescale();
}

void ImageItemCard::mouseDoubleClickEvent(QMouseEvent*) { emit activated(itemId()); }

}  // namespace napkin
