#include "BackgroundSaver.h"
#include "../data/BufferRepository.h"
#include "../data/Database.h"
#include "../data/ItemRepository.h"
#include "../domain/Clock.h"

#include <QCoreApplication>
#include <exception>

namespace napkin {

BackgroundSaver::BackgroundSaver(const Database& db, QObject* parent) : QObject(parent)
{
    if (db.isOpen()) {
        // Empty for ":memory:" and for a temporary database.
        if (const char* file = sqlite3_db_filename(db.handle(), "main"); file && *file)
            path_ = QString::fromUtf8(file);
    }
    pool_.setMaxThreadCount(1);   // one at a time, in the order they were asked for
    connection_ = std::make_shared<Database>();
}

BackgroundSaver::~BackgroundSaver()
{
    // Never dropped: a queued save is the user's typing.
    pool_.waitForDone();
}

void BackgroundSaver::save(BufferId buffer, std::vector<Entry> entries)
{
    ++inFlight_;
    auto connection = connection_;
    const QString path = path_;
    pool_.start([this, connection, path, buffer, entries = std::move(entries)] {
        QString error;
        Timestamp when = 0;
        try {
            if (!connection->isOpen()) {
                connection->open(path);   // sets Database's default busy_timeout
                appliedBusyTimeoutMs_ = 0;
            }
            // A test override, applied once per value rather than on every save.
            if (const int ms = busyTimeoutMs_.load(); ms > 0 && ms != appliedBusyTimeoutMs_) {
                connection->exec(QString("PRAGMA busy_timeout=%1;").arg(ms).toUtf8().constData());
                appliedBusyTimeoutMs_ = ms;
            }
            ItemRepository items(*connection);
            BufferRepository buffers(*connection);
            Transaction tx(*connection);
            for (const auto& e : entries) items.updateText(e.id, e.text);
            buffers.touch(buffer);
            tx.commit();
            when = nowMs();
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
            if (error.isEmpty()) error = QStringLiteral("unknown error");
        }
        QMetaObject::invokeMethod(this, [this, buffer, entries, when, error] {
            --inFlight_;
            if (error.isEmpty()) emit saved(buffer, entries, when);
            else emit failed(error);
        }, Qt::QueuedConnection);
    });
}

void BackgroundSaver::waitForIdle()
{
    while (inFlight_ > 0) {
        pool_.waitForDone();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
}

}  // namespace napkin
