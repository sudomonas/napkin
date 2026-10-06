#pragma once
#include "../domain/Types.h"
#include <QDeadlineTimer>
#include <QSet>
#include <QWidget>
#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;

namespace napkin {

// SPEC.md §6. For an application whose premise is throwing things in without
// thinking, accidental deletion is the fastest way to lose a user's trust — so
// every delete is undoable for a few seconds, in one click, without hunting for
// the trash.
//
// Offers STACK. Each one used to replace the last, so two deletes in quick
// succession left only the second undoable — the first was gone the moment the
// second was made, however fast you reached for Ctrl+Z. Now each offer lives
// out its own eight seconds; the toast shows the newest and says how many are
// behind it, and Undo works back through them newest first, as undo does
// everywhere else.
class UndoToast : public QWidget {
    Q_OBJECT
public:
    static constexpr int kVisibleMs = 8000;
    // A message with nothing to undo is an acknowledgement, not an offer, so it
    // shows for about as long as a card's own flash rather than for eight
    // seconds — and never for longer than the offer it is standing in front of.
    static constexpr int kInformMs = 1600;

    explicit UndoToast(QWidget* parent = nullptr);

    // Adds an offer on top of any already showing. The action is a closure so
    // buffer-level and item-level undo share one widget rather than one growing
    // an enum. `protects` are blobs the undo would need back; see
    // protectedHashes().
    // Returns the offer's id, for withdraw(); 0 when there was nothing to undo.
    int  offer(const QString& message, std::function<void()> undo,
               const QSet<QString>& protects = {});
    // Takes one offer back without running it — for when what it would undo
    // has been finished some other way. The others stay.
    void withdraw(int id);
    // A message with nothing to undo — "Restored to …". No Undo button, and the
    // offers behind it keep their countdowns.
    void inform(const QString& message);
    // Drops every offer. For when what they would undo no longer exists.
    void dismiss();
    // Ctrl+Z. Undoes the newest offer and leaves the rest. Returns whether there
    // was anything to undo.
    bool undoNow();

    // Re-centres without touching the message or restarting the countdown.
    void reposition();

    bool hasOffer() const { return !offers_.empty(); }
    void setVisibleMsForTest(int ms) { visibleMs_ = ms; }
    int  offerCount() const { return int(offers_.size()); }
    // Every blob some live offer would put back. A sweep must leave these
    // alone: their rows are gone, so to the sweep they look like orphans.
    QSet<QString> protectedHashes() const;

signals:
    void undone();
    void expired();

protected:
    void paintEvent(QPaintEvent* e) override;
    // The countdown pauses while the pointer is on the toast: someone reaching
    // for Undo must not have it vanish under the cursor.
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    struct Offer {
        int id = 0;
        QString message;
        std::function<void()> undo;
        QSet<QString> protects;
        QDeadlineTimer deadline;   // running, or...
        qint64 remainingMs = 0;    // ...frozen while paused
    };

    void pause();
    void resume();
    void expireDue();
    // Shows the newest offer, or hides when there is none.
    void showTop();
    void scheduleNextExpiry();

    QLabel*      message_ = nullptr;
    QPushButton* undoButton_ = nullptr;
    QTimer*      timer_   = nullptr;   // fires at the earliest deadline
    QTimer*      informTimer_ = nullptr;
    std::vector<Offer> offers_;        // oldest first
    bool hovered_ = false;
    bool informing_ = false;
    int  nextId_ = 1;
    int  visibleMs_ = kVisibleMs;
};

}  // namespace napkin
