#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>
#include <memory>

namespace napkin {

// Two processes on one SQLite file is easy to forget and painful to debug
// (SPEC.md §10). A second launch hands off to the first and exits.
//
// Who is first is decided by a lock file held for the life of the process, not
// by the socket. The socket alone was a guess: two launches at once could both
// find nobody listening, and the loser of the race to listen deleted the
// winner's socket and listened in its place — two Napkins on one database. And
// a primary that took longer than 300 ms to answer was taken for dead. The lock
// is released by the operating system when the process ends however it ends,
// so a crash never leaves Napkin unable to start.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    // Where the lock and socket live; the data directory unless a test says
    // otherwise.
    SingleInstance(const QString& directory, QObject* parent);

    // The local-server name a primary for `directory` listens on: a path in the
    // data directory (or the runtime directory, when that path is too long for
    // a Unix socket), or on Windows a named pipe named from its hash.
    static QString serverNameFor(const QString& directory);
    ~SingleInstance() override;

    enum class Outcome {
        Primary,        // this process is Napkin; carry on
        HandedOff,      // another Napkin is running and was asked to raise itself
        NotResponding,  // another Napkin holds the lock but did not answer
    };
    Outcome acquire(int answerTimeoutMs = 3000);

    // For tests: a primary whose window has not started listening yet.
    void stopListeningForTest() { server_.close(); }

signals:
    void raiseRequested();

private:
    QString      key_;
    std::unique_ptr<QLockFile> lock_;
    QLocalServer server_;
};

}  // namespace napkin
