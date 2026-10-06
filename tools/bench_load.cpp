// Measures Napkin against a seeded profile. Reports §12's targets plus the
// costs that only appear at scale: cold thumbnail generation, opening a buffer
// with a hundred items, and scrolling a list that does not fit in memory twice.
//
//   XDG_DATA_HOME=<profile>/share bench_load

#include "app/Paths.h"
#include "data/BufferRepository.h"
#include "data/Database.h"
#include "data/ItemRepository.h"
#include "data/Statement.h"
#include "domain/BufferService.h"
#include "media/BlobStore.h"
#include "media/Thumbnailer.h"
#include "ui/BufferListModel.h"
#include "ui/BufferListView.h"
#include "ui/ItemCanvas.h"
#include "ui/ItemCard.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QScrollBar>
#include <QTimer>
#include <QPixmapCache>
#include <cstdio>
#ifdef __GLIBC__
#  include <malloc.h>
#endif

using namespace napkin;

namespace {

qint64 rssKb()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray& line : f.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.split(':').at(1).trimmed().split(' ').at(0).toLongLong();
    return -1;
}

// processEvents() does NOT dispatch DeferredDelete — Qt holds those until the
// event loop unwinds to the level that posted them. A harness that only calls
// processEvents() therefore accumulates every widget the canvas retired, and
// reports it as though the application had leaked it. Drain them explicitly.
void settle(int ms = 60)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void row(const char* label, qint64 value, const char* unit, qint64 budget = -1)
{
    const char* verdict = budget < 0 ? "" : (value <= budget ? "  ok" : "  OVER");
    if (budget < 0) printf("  %-42s %7lld %-3s\n", label, value, unit);
    else printf("  %-42s %7lld %-3s  (target %lld)%s\n", label, value, unit, budget, verdict);
    fflush(stdout);
}

void mem(const char* label, qint64 base)
{
    printf("  %-42s %7lld MB  <- RSS\n", label, (rssKb() - base) / 1024);
    fflush(stdout);
}

}  // namespace

int main(int argc, char** argv)
{
    const qint64 rssStart = rssKb();
    QElapsedTimer boot; boot.start();

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("napkin"));
    QCoreApplication::setOrganizationName(QStringLiteral("napkin"));

    paths::ensureDirs();
    Database db;
    db.open(paths::databaseFile());
    const qint64 dbOpenMs = boot.elapsed();

    BufferRepository buffers(db);
    ItemRepository items(db);
    BufferService service(db, buffers, items);
    BlobStore blobs(paths::blobsDir());
    Thumbnailer thumbs(paths::thumbsDir(), blobs);

    QElapsedTimer t; t.start();
    MainWindow w(db, buffers, items, service, blobs, thumbs);
    const qint64 ctorMs = t.elapsed();

    t.restart();
    w.resize(1280, 860);
    w.show();
    settle(200);
    const qint64 toInteractiveMs = boot.elapsed();

    auto* model = w.findChild<BufferListModel*>();
    auto* view = w.findChild<BufferListView*>();
    auto* canvas = w.findChild<ItemCanvas*>();
    auto* search = w.findChild<QLineEdit*>();

    // Corpus shape, so the numbers below can be read against something.
    qint64 itemCount = 0, imageCount = 0, biggest = 0; BufferId biggestId = kNoBuffer;
    {
        Statement s(db, "SELECT buffer_id, COUNT(*), SUM(type='image') FROM items GROUP BY buffer_id");
        while (s.step()) {
            const qint64 n = s.columnInt64(1);
            itemCount += n;
            imageCount += s.columnInt64(2);
            if (n > biggest) { biggest = n; biggestId = BufferId(s.columnInt64(0)); }
        }
    }

    printf("\ncorpus\n");
    row("buffers", model->rowCount(), "");
    row("items", itemCount, "");
    row("image items", imageCount, "");
    row("largest buffer", biggest, "items");
    printf("\nstartup\n");
    row("database open + migrate", dbOpenMs, "ms");
    row("MainWindow construction", ctorMs, "ms");
    row("cold start to interactive", toInteractiveMs, "ms", 300);
    row("RSS after start", (rssKb() - rssStart) / 1024, "MB", 120);

    // --- scrolling the buffer list, thumbnails cold -------------------------
    printf("\nbuffer list\n");
    t.restart();
    qint64 worstRow = 0;
    QElapsedTimer rowTimer;
    for (int r = 0; r < model->rowCount(); ++r) {
        rowTimer.restart();
        view->scrollTo(model->index(r, 0), QAbstractItemView::PositionAtCenter);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        worstRow = std::max(worstRow, rowTimer.elapsed());
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const qint64 coldScroll = t.elapsed();
    row("scroll every row, thumbnails cold", coldScroll, "ms");
    row("  per row", coldScroll / std::max(1, model->rowCount()), "ms");
    row("  longest freeze during it", worstRow, "ms", 50);
    thumbs.waitForIdle();

    t.restart();
    for (int r = model->rowCount() - 1; r >= 0; --r) {
        view->scrollTo(model->index(r, 0), QAbstractItemView::PositionAtCenter);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    const qint64 warmScroll = t.elapsed();
    row("scroll every row, thumbnails warm", warmScroll, "ms");
    row("  per row", warmScroll / std::max(1, model->rowCount()), "ms");
    row("RSS after full scroll", (rssKb() - rssStart) / 1024, "MB", 120);

    // --- opening buffers ----------------------------------------------------
    printf("\nopening a buffer\n");
    qint64 worst = 0, sum = 0; int opened = 0;
    for (int r = 0; r < std::min(40, model->rowCount()); ++r) {
        t.restart();
        view->setCurrentIndex(model->index(r, 0));
        settle(30);
        const qint64 ms = t.elapsed();
        worst = std::max(worst, ms); sum += ms; ++opened;
    }
    row("median-ish open (40 buffers, mean)", sum / std::max(1, opened), "ms");
    row("worst of those 40", worst, "ms");
    mem("after opening 40 buffers", rssStart);

    const int bigRow = model->rowForId(biggestId);
    if (bigRow >= 0) {
        t.restart();
        view->setCurrentIndex(model->index(bigRow, 0));
        settle(60);
        row("open the largest buffer", t.elapsed(), "ms");
        row("  cards materialized", canvas->findChildren<ItemCard*>().size(), "");
        mem("  after opening it", rssStart);

        // Scrolling a board that is mostly off screen is what virtualization is
        // for; if it is working, this stays flat.
        // Three passes, not one: a cache that fills and then plateaus is not a
        // leak, and RSS alone cannot tell them apart from a single sample.
        auto* bar = canvas->verticalScrollBar();
        for (int pass = 1; pass <= 3; ++pass) {
            t.restart();
            // The longest the window was unresponsive for, which is what a
            // scroll feels like. Total time alone stopped meaning that once
            // previews were made on a worker: it is mostly waiting.
            qint64 worstStep = 0;
            QElapsedTimer step;
            for (int v = bar->minimum(); v <= bar->maximum(); v += std::max(1, bar->maximum() / 40)) {
                step.restart();
                bar->setValue(v);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                worstStep = std::max(worstStep, step.elapsed());
            }
            bar->setValue(bar->minimum());
            settle(40);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            const qint64 scrolled = t.elapsed();
            thumbs.waitForIdle();
            char label[96];
            snprintf(label, sizeof(label), "  scroll board top to bottom, pass %d", pass);
            row(label, scrolled, "ms");
            row("    longest freeze during it", worstStep, "ms", 50);
            row("    previews all made after", t.elapsed(), "ms");
            snprintf(label, sizeof(label), "    after pass %d", pass);
            mem(label, rssStart);
        }
        row("  cards materialized after scrolling", canvas->findChildren<ItemCard*>().size(), "");
    }

    // --- search -------------------------------------------------------------
    printf("\nsearch\n");
    for (const char* q : {"invoice", "postgres", "deploy stag", "zzzz"}) {
        search->clear();
        settle(20);
        t.restart();
        model->setQuery(QString::fromLatin1(q));
        const qint64 ms = t.elapsed();
        char label[96];
        snprintf(label, sizeof(label), "query \"%s\" -> %d buffers", q, model->rowCount());
        row(label, ms, "ms", 100);
    }
    model->setQuery(QString());
    mem("after search", rssStart);

    printf("\nfootprint\n");
    row("RSS at end", (rssKb() - rssStart) / 1024, "MB");
    // The application trims the heap itself once preview work goes quiet; this
    // is what a system monitor shows a couple of seconds after scrolling stops.
    settle(2500);
    row("RSS after 2.5 s idle", (rssKb() - rssStart) / 1024, "MB", 120);
    row("QPixmapCache limit", QPixmapCache::cacheLimit() / 1024, "MB");

    // Distinguishes memory the application is still holding from memory glibc
    // has simply not handed back. Decoding a 2560x1440 image transiently needs
    // ~15 MB, and a heap that has seen hundreds of those keeps the arenas.
    QPixmapCache::clear();
    settle(60);
    row("RSS after clearing the pixmap cache", (rssKb() - rssStart) / 1024, "MB");
#ifdef __GLIBC__
    // glibc only; the question it answers is about glibc's arenas.
    malloc_trim(0);
    row("RSS after malloc_trim", (rssKb() - rssStart) / 1024, "MB", 120);
#endif
    printf("\n");
    return 0;
}
