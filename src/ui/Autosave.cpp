#include "Autosave.h"

namespace napkin {

Autosave::Autosave(QObject* parent) : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounceMs);
    maxDelay_.setSingleShot(true);
    maxDelay_.setInterval(kMaxDelayMs);

    connect(&debounce_, &QTimer::timeout, this, [this] { doFlush(true); });
    connect(&maxDelay_, &QTimer::timeout, this, [this] { doFlush(true); });
}

void Autosave::noteChange()
{
    dirty_ = true;
    debounce_.start();                              // restarts on every keystroke
    if (!maxDelay_.isActive()) maxDelay_.start();   // but this one does not
}

// Always reaches the handler, even with nothing new pending here: the last
// timed flush may have handed text to the background, and "now" means that has
// landed too. Skipping it when this flag was clear made every flushNow() — an
// image pasted mid-save, the window losing focus — return before the text was
// written (independent review).
void Autosave::flushNow()
{
    debounce_.stop();
    maxDelay_.stop();
    dirty_ = false;
    if (flush_) flush_(false);
}

void Autosave::doFlush(bool timed)
{
    debounce_.stop();
    maxDelay_.stop();
    if (!dirty_) return;
    dirty_ = false;
    if (flush_) flush_(timed);
}

}  // namespace napkin
