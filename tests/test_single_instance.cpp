#include "../src/app/SingleInstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cstdio>
#include <cstdlib>

using namespace napkin;

// Who owns the data directory. Decided by a lock the operating system holds for
// the life of the process; the socket only carries the "raise yourself" knock.
class TestSingleInstance : public QObject {
    Q_OBJECT
private slots:
    void theFirstIsPrimaryAndTheSecondHandsOff()
    {
        QTemporaryDir dir;
        SingleInstance first(dir.path(), nullptr);
        QCOMPARE(first.acquire(), SingleInstance::Outcome::Primary);
        QSignalSpy raised(&first, &SingleInstance::raiseRequested);

        SingleInstance second(dir.path(), nullptr);
        QCOMPARE(second.acquire(1000), SingleInstance::Outcome::HandedOff);
        QTRY_COMPARE_WITH_TIMEOUT(raised.size(), 1, 2000);
    }

    // A data directory under a long home path left no room in sockaddr_un, so
    // the primary could not listen and every second launch found it "not
    // responding". Found by running the real binary against a deep scratch
    // profile; the tests above all used short temporary paths.
    void aDataDirectoryTooDeepForASocketPathStillHandsOff()
    {
        if (qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR"))
            QSKIP("no per-user runtime directory to fall back to");
        QTemporaryDir root;
        const QString deep = root.path() + QStringLiteral("/a-rather-long-home-directory-name"
                                                          "/.local/share/napkin/napkin-profile/deeper-still");
        QVERIFY(QDir().mkpath(deep));
        QVERIFY((deep + QStringLiteral("/napkin.sock")).toLocal8Bit().size() > 108);

        SingleInstance first(deep, nullptr);
        QCOMPARE(first.acquire(), SingleInstance::Outcome::Primary);
        QSignalSpy raised(&first, &SingleInstance::raiseRequested);
        SingleInstance second(deep, nullptr);
        QCOMPARE(second.acquire(1000), SingleInstance::Outcome::HandedOff);
        QTRY_COMPARE_WITH_TIMEOUT(raised.size(), 1, 2000);
    }

    // Napkin 0.1.7 and earlier never took the lock; they only listened. A new
    // build that took the free lock and then deleted the old one's socket
    // started a second Napkin on the same database (independent review).
    void anOlderNapkinThatOnlyListensIsStillFoundAndRaised()
    {
        QTemporaryDir dir;
        QProcess old;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("NAPKIN_TEST_OLD_PRIMARY"), dir.path());
        old.setProcessEnvironment(env);
        old.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(old.waitForReadyRead(5000));
        QCOMPARE(old.readLine().trimmed(), QByteArray("listening"));

        SingleInstance next(dir.path(), nullptr);
        QCOMPARE(next.acquire(1000), SingleInstance::Outcome::HandedOff);
        QVERIFY(old.waitForReadyRead(3000));
        QCOMPARE(old.readLine().trimmed(), QByteArray("knocked"));
        old.kill();
        old.waitForFinished();
    }

    // The race the socket could not settle: a primary that is not answering
    // yet — or never will — used to be taken for dead, its socket deleted, and
    // a second Napkin started on the same database.
    void aPrimaryThatIsNotAnsweringIsStillThePrimary()
    {
        QTemporaryDir dir;
        SingleInstance first(dir.path(), nullptr);
        QCOMPARE(first.acquire(), SingleInstance::Outcome::Primary);
        first.stopListeningForTest();

        SingleInstance second(dir.path(), nullptr);
        QElapsedTimer waited;
        waited.start();
        QCOMPARE(second.acquire(600), SingleInstance::Outcome::NotResponding);
        // It kept knocking for as long as it was told to, not one attempt.
        QVERIFY(waited.elapsed() >= 500);
    }

    // The lock outlives nothing: a Napkin killed outright must not stop the
    // next one starting. Held by a separate process, which is then killed —
    // a lock released by our own destructor would prove nothing.
    void aKilledPrimaryDoesNotLockTheNextOneOut()
    {
        QTemporaryDir dir;
        QProcess holder;
        holder.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("NAPKIN_TEST_HOLD_LOCK"), dir.path());
        holder.setProcessEnvironment(env);
        holder.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(holder.waitForStarted());
        QVERIFY(holder.waitForReadyRead(5000));
        QCOMPARE(holder.readLine().trimmed(), QByteArray("holding"));

        {
            SingleInstance meanwhile(dir.path(), nullptr);
            QVERIFY(meanwhile.acquire(300) != SingleInstance::Outcome::Primary);
        }

        holder.kill();   // SIGKILL: no destructor, no cleanup, the lock file stays
        QVERIFY(holder.waitForFinished());
        QVERIFY(QFile::exists(dir.path() + QStringLiteral("/napkin.lock")));

        SingleInstance next(dir.path(), nullptr);
        QCOMPARE(next.acquire(300), SingleInstance::Outcome::Primary);
    }
};

int main(int argc, char** argv)
{
    // The holder half of aKilledPrimaryDoesNotLockTheNextOneOut.
    if (const char* hold = std::getenv("NAPKIN_TEST_HOLD_LOCK")) {
        QCoreApplication app(argc, argv);
        SingleInstance instance(QString::fromLocal8Bit(hold), nullptr);
        if (instance.acquire() != SingleInstance::Outcome::Primary) return 2;
        std::printf("holding\n");
        std::fflush(stdout);
        QThread::sleep(60);
        return 0;
    }
    // An old Napkin: listens on the socket, holds no lock.
    if (const char* dir = std::getenv("NAPKIN_TEST_OLD_PRIMARY")) {
        QCoreApplication app(argc, argv);
        QLocalServer server;
        if (!server.listen(QString::fromLocal8Bit(dir) + QStringLiteral("/napkin.sock"))) return 2;
        QObject::connect(&server, &QLocalServer::newConnection, [] {
            std::printf("knocked\n");
            std::fflush(stdout);
        });
        std::printf("listening\n");
        std::fflush(stdout);
        return app.exec();
    }
    QCoreApplication app(argc, argv);
    TestSingleInstance test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_single_instance.moc"
