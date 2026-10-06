#include "SingleInstance.h"
#include "Paths.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QLocalSocket>
#include <QThread>

namespace napkin {

SingleInstance::SingleInstance(QObject* parent) : SingleInstance(paths::dataDir(), parent) {}

SingleInstance::SingleInstance(const QString& directory, QObject* parent) : QObject(parent)
{
    // Not a bare name: Qt would put that in a shared temp directory when
    // XDG_RUNTIME_DIR is unset, world-connectable, where any local user can
    // create it first and stop Napkin starting at all. Under the 0700 data
    // directory it is ours alone.
#ifdef Q_OS_WIN
    // On Windows a local server is a named pipe, which lives in a flat,
    // machine-wide namespace rather than in a directory. The pipe is named after
    // the data directory, which contains the user's profile path, so two users
    // (or a test profile) get different pipes; UserAccessOption puts an ACL on
    // it so only this user can connect.
    key_ = QStringLiteral("io.github.sudomonas.Napkin-")
         + QString::fromLatin1(QCryptographicHash::hash(directory.toUtf8(),
                                                        QCryptographicHash::Sha256)
                                   .toHex().left(32));
#else
    key_ = directory + QStringLiteral("/napkin.sock");
    // A Unix socket's path has to fit in sockaddr_un — 108 bytes on Linux, 104
    // on macOS — and a data directory under a long home path does not leave
    // room. listen() then failed, so the primary could never be reached and
    // every second launch reported it as not responding. The per-user runtime
    // directory is private to this user too (0700, owned by them), and the
    // name still ties the socket to this data directory.
    if (key_.toLocal8Bit().size() > 100) {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!runtime.isEmpty())
            key_ = runtime + QStringLiteral("/napkin-")
                 + QString::fromLatin1(QCryptographicHash::hash(directory.toUtf8(),
                                                                QCryptographicHash::Sha256)
                                           .toHex().left(16))
                 + QStringLiteral(".sock");
    }
#endif
    // QLockFile holds a native lock on the file as well as writing its owner
    // into it, and a lock whose owner has died is recognised as stale and taken
    // over. Zero means it is never stale by AGE alone: a Napkin left running
    // for a month still owns its data.
    lock_ = std::make_unique<QLockFile>(directory + QStringLiteral("/napkin.lock"));
    lock_->setStaleLockTime(0);

    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        if (auto* c = server_.nextPendingConnection()) {
            connect(c, &QLocalSocket::disconnected, c, &QLocalSocket::deleteLater);
            emit raiseRequested();
        }
    });
}

SingleInstance::~SingleInstance() = default;

SingleInstance::Outcome SingleInstance::acquire(int answerTimeoutMs)
{
    if (lock_->tryLock(0)) {
        // Holding the lock is not quite proof nobody else is here: Napkin 0.1.7
        // and earlier never took it, only listened. Taking over the socket of
        // a running older Napkin would put two on one database — exactly what
        // this exists to prevent — so knock once before claiming it.
        {
            QLocalSocket probe;
            probe.connectToServer(key_);
            if (probe.waitForConnected(300)) {
                probe.write("raise");
                probe.waitForBytesWritten(300);
                probe.disconnectFromServer();
                lock_->unlock();
                return Outcome::HandedOff;
            }
        }
        // Nobody answered, and we own the data. Any socket file left here
        // belonged to a Napkin that has since died, so it is safe to take over.
        QLocalServer::removeServer(key_);
        server_.listen(key_);   // if this fails we are still the only Napkin
        return Outcome::Primary;
    }

    // Another Napkin owns the data. It may still be starting — the lock is
    // taken before its window exists — so keep asking for a while rather than
    // concluding from one unanswered knock that it is gone.
    QDeadlineTimer deadline(answerTimeoutMs);
    do {
        QLocalSocket probe;
        probe.connectToServer(key_);
        if (probe.waitForConnected(200)) {
            probe.write("raise");
            probe.waitForBytesWritten(300);
            probe.disconnectFromServer();
            return Outcome::HandedOff;
        }
        QThread::msleep(100);
    } while (!deadline.hasExpired());
    return Outcome::NotResponding;
}

}  // namespace napkin
