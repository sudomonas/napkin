#include "../src/data/BufferRepository.h"
#include "../src/data/ItemRepository.h"
#include "../src/domain/BufferService.h"
#include "../src/media/BlobStore.h"
#include "../src/media/Exporter.h"
#include "TestDb.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QtTest>

using namespace napkin;

// SPEC.md §13: local-first without an exit is its own kind of lock-in. The
// output has to be readable by anything, and honest about what it could not
// write.
class TestExport : public QObject {
    Q_OBJECT
private:
    QByteArray pngBytes()
    {
        QImage img(8, 6, QImage::Format_RGB32);
        img.fill(Qt::red);
        QByteArray out;
        QBuffer buf(&out);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "png");
        return out;
    }

    QJsonObject manifestIn(const QString& dir)
    {
        QFile f(QDir(dir).filePath(QStringLiteral("manifest.json")));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }

private slots:
    void aBufferBecomesAFolderOfOrdinaryFiles()
    {
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        const auto id = buffers.create(/*pinned=*/true);
        service.appendTo(id, Item::makeText(QStringLiteral("deploy notes for friday")));
        const auto stored = blobs.store(pngBytes(), QStringLiteral("image/png"));
        QVERIFY(stored.ok);
        service.appendTo(id, Item::makeImage(stored.hash, stored.size.width(),
                                             stored.size.height(), stored.byteSize,
                                             QStringLiteral("screenshot"), stored.mime));

        Exporter exporter(buffers, items, blobs);
        const auto result = exporter.exportBuffer(id, outDir.path());

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.problems.isEmpty());
        QCOMPARE(result.items, 2);
        QVERIFY(QFileInfo(result.rootDir).fileName().endsWith(QStringLiteral("deploy-notes-for-friday")));

        // A napkin with a name of its own is exported under that name.
        buffers.setName(id, QStringLiteral("Friday release"));
        const auto named = exporter.exportBuffer(id, outDir.path());
        QVERIFY2(QFileInfo(named.rootDir).fileName().endsWith(QStringLiteral("friday-release")),
                 qPrintable(named.rootDir));
        QCOMPARE(result.images, 1);

        const QStringList written = QDir(result.rootDir).entryList(QDir::Files, QDir::Name);
        QCOMPARE(written.size(), 3);   // the note, the image, the manifest
        QVERIFY(written.contains(QStringLiteral("manifest.json")));
        QVERIFY(written.contains(QStringLiteral("001-deploy-notes-for-friday.txt")));
        QVERIFY(written.contains(QStringLiteral("002-screenshot.png")));

        // The text is the text — no wrapper, no escaping, openable by anything.
        QFile note(QDir(result.rootDir).filePath(QStringLiteral("001-deploy-notes-for-friday.txt")));
        QVERIFY(note.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(note.readAll()), QStringLiteral("deploy notes for friday"));
    }

    void theManifestCarriesWhatTheFilesystemCannot()
    {
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        const auto id = buffers.create(/*pinned=*/true, /*kept=*/true);
        service.appendTo(id, Item::makeText(QStringLiteral("first")));
        service.appendTo(id, Item::makeText(QStringLiteral("second")));

        Exporter exporter(buffers, items, blobs);
        const auto result = exporter.exportBuffer(id, outDir.path());
        const QJsonObject manifest = manifestIn(result.rootDir);

        QCOMPARE(manifest["napkin_export_version"].toInt(), 1);
        QVERIFY(manifest["buffer"].toObject()["pinned"].toBool());
        QVERIFY(manifest["buffer"].toObject()["kept"].toBool());

        // Ordering is the thing a folder of files loses, so it is the thing the
        // manifest most has to keep.
        const QJsonArray entries = manifest["items"].toArray();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].toObject()["position"].toInt(), 0);
        QCOMPARE(entries[1].toObject()["position"].toInt(), 1);
        QVERIFY(entries[0].toObject()["file"].toString().startsWith(QStringLiteral("001-")));

        // ISO-8601, not milliseconds since an epoch: an export is meant to be
        // read by a person and by tools that are not Napkin.
        QVERIFY(QDateTime::fromString(manifest["exported_at"].toString(), Qt::ISODate).isValid());
    }

    void userContentCannotEscapeTheExportFolder()
    {
        // The filename comes from the user's text, which is the one place in
        // Napkin where content becomes a path.
        QCOMPARE(Exporter::slug(QStringLiteral("../../etc/passwd")),
                 QStringLiteral("etc-passwd"));
        QCOMPARE(Exporter::slug(QStringLiteral("/absolute/path")),
                 QStringLiteral("absolute-path"));
        QCOMPARE(Exporter::slug(QStringLiteral("..")), QStringLiteral("item"));
        QCOMPARE(Exporter::slug(QStringLiteral("   ")), QStringLiteral("item"));
        QCOMPARE(Exporter::slug(QString()), QStringLiteral("item"));

        // A NUL truncates a path in every C API underneath Qt.
        QString withNul = QStringLiteral("safe");
        withNul.append(QChar(0)).append(QStringLiteral("evil"));
        QCOMPARE(Exporter::slug(withNul), QStringLiteral("safe-evil"));

        // Reserved on Windows even without an extension, and Napkin is meant to
        // get there in Phase 9.
        QCOMPARE(Exporter::slug(QStringLiteral("CON")), QStringLiteral("item"));
        QCOMPARE(Exporter::slug(QStringLiteral("nul")), QStringLiteral("item"));

        // Long enough to break a filesystem, and non-Latin scripts survive.
        QVERIFY(Exporter::slug(QString(500, u'a')).size() <= 40);
        QCOMPARE(Exporter::slug(QStringLiteral("日本語 メモ")), QStringLiteral("日本語-メモ"));
    }

    void exportingTwiceDoesNotOverwriteTheFirstOne()
    {
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        const auto id = buffers.create();
        service.appendTo(id, Item::makeText(QStringLiteral("same name")));

        Exporter exporter(buffers, items, blobs);
        const auto first = exporter.exportBuffer(id, outDir.path());
        const auto second = exporter.exportBuffer(id, outDir.path());

        QVERIFY(first.ok && second.ok);
        QVERIFY2(first.rootDir != second.rootDir, qPrintable(first.rootDir));
        QVERIFY(QDir(first.rootDir).exists());
    }

    void aMissingImageIsReportedRatherThanPassedOver()
    {
        // The failure that matters most: an export that silently skipped an
        // image would be a backup that quietly is not one.
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        const auto id = buffers.create();
        service.appendTo(id, Item::makeImage(QStringLiteral("0")
                                                 + QString(63, u'a'),
                                             10, 10, 99, QStringLiteral("gone"),
                                             QStringLiteral("image/png")));

        Exporter exporter(buffers, items, blobs);
        const auto result = exporter.exportBuffer(id, outDir.path());

        QCOMPARE(result.images, 0);
        QCOMPARE(result.problems.size(), 1);
        QVERIFY(result.problems.first().contains(QStringLiteral("missing")));
    }

    void anExportedFolderStandsOnItsOwn()
    {
        // The test §13 is actually asking for: could someone recover their
        // content from this folder with no Napkin present? So read it back the
        // way a stranger would — the manifest names the files, the files hold
        // the bytes, and the bytes are the originals.
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        const QByteArray original = pngBytes();
        const auto id = buffers.create();
        service.appendTo(id, Item::makeText(QStringLiteral("line one\nline two")));
        const auto stored = blobs.store(original, QStringLiteral("image/png"));
        service.appendTo(id, Item::makeImage(stored.hash, stored.size.width(),
                                             stored.size.height(), stored.byteSize,
                                             QString(), stored.mime));

        Exporter exporter(buffers, items, blobs);
        const auto result = exporter.exportBuffer(id, outDir.path());
        QVERIFY(result.ok);

        const QJsonArray entries = manifestIn(result.rootDir)["items"].toArray();
        QCOMPARE(entries.size(), 2);

        for (const auto& value : entries) {
            const QJsonObject entry = value.toObject();
            QFile f(QDir(result.rootDir).filePath(entry["file"].toString()));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(entry["file"].toString()));
            const QByteArray bytes = f.readAll();

            if (entry["type"].toString() == QLatin1String("text")) {
                QCOMPARE(QString::fromUtf8(bytes), QStringLiteral("line one\nline two"));
            } else {
                // Byte-for-byte, not re-encoded — otherwise the hash in the
                // manifest stops being checkable against the file beside it.
                QCOMPARE(bytes, original);
                QCOMPARE(QString::fromLatin1(
                             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()),
                         entry["sha256"].toString());
            }
        }
    }

    void itemsComeOutInTheOrderTheyWentIn()
    {
        // listForBuffer() sorts newest-first because that is how the board
        // reads. A folder of files is read from 001 downwards, so an export
        // that inherited the board's order inverted every buffer on its way
        // out — which it did, until this caught it.
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        const auto id = buffers.create();
        for (const char* t : {"first", "second", "third"})
            service.appendTo(id, Item::makeText(QString::fromLatin1(t)));

        Exporter exporter(buffers, items, blobs);
        const auto result = exporter.exportBuffer(id, outDir.path());

        const QJsonArray entries = manifestIn(result.rootDir)["items"].toArray();
        QCOMPARE(entries[0].toObject()["file"].toString(), QStringLiteral("001-first.txt"));
        QCOMPARE(entries[1].toObject()["file"].toString(), QStringLiteral("002-second.txt"));
        QCOMPARE(entries[2].toObject()["file"].toString(), QStringLiteral("003-third.txt"));
    }

    void exportAllGivesOneFolderPerBuffer()
    {
        TestDb db;
        BufferRepository buffers(db.db);
        ItemRepository items(db.db);
        BufferService service(db.db, buffers, items);
        QTemporaryDir blobDir, outDir;
        BlobStore blobs(blobDir.path());

        for (int i = 0; i < 5; ++i) {
            const auto id = buffers.create();
            service.appendTo(id, Item::makeText(QStringLiteral("buffer %1").arg(i)));
        }
        const auto trashed = buffers.create();
        service.appendTo(trashed, Item::makeText(QStringLiteral("deleted")));
        service.trash(trashed);

        Exporter exporter(buffers, items, blobs);
        const auto result = exporter.exportAll(outDir.path());

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.buffers, 5);   // what is in the trash is not "everything"
        QCOMPARE(QDir(result.rootDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 5);
    }
};

QTEST_MAIN(TestExport)
#include "test_export.moc"
