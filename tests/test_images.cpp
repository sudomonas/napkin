#include "GuiFixture.h"
#include "../src/media/BlobGc.h"
#include "../src/media/ClipboardContent.h"
#include "../src/media/ImageFormats.h"
#include "../src/ui/BufferCardDelegate.h"
#include "../src/ui/Lightbox.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QLabel>
#include <QMovie>
#include <QThread>
#include <QTimer>
#include <QtTest>

using namespace napkin;

namespace {

QByteArray makePng(int w, int h, QColor colour = Qt::red)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

QImage makePngImage()
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::gray);
    return image;
}

QByteArray makeJpeg(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 90);
    return out;
}

// A hand-assembled two-frame GIF: Qt has no GIF writer, so one is built here.
QByteArray makeAnimatedGif()
{
    auto blocks = [](QByteArray d) {
        QByteArray out;
        while (!d.isEmpty()) {
            const int n = std::min(255, int(d.size()));
            out.append(char(n)).append(d.left(n));
            d = d.mid(n);
        }
        return out.append('\0');
    };
    const int w = 24, h = 24;
    QByteArray g("GIF89a", 6);
    auto le16 = [&](int v) { g.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)); };
    le16(w); le16(h);
    g.append(char(0x80)).append('\0').append('\0');
    g.append("\xFF\x00\x00\x00\x00\xFF", 6);                     // 2-colour table
    g.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);  // loop forever

    for (int frame = 0; frame < 2; ++frame) {
        g.append("\x21\xF9\x04\x00\x32\x00\x00\x00", 8);       // 500ms delay
        g.append(char(0x2C)); le16(0); le16(0); le16(w); le16(h); g.append('\0');
        constexpr int minCode = 2;
        QList<int> codes{1 << minCode};
        for (int i = 0; i < w * h; ++i) codes << frame;
        codes << ((1 << minCode) + 1);
        QByteArray packed;
        quint32 acc = 0; int bits = 0;
        for (int c : codes) {
            acc |= quint32(c) << bits; bits += minCode + 1;
            while (bits >= 8) { packed.append(char(acc & 0xFF)); acc >>= 8; bits -= 8; }
        }
        if (bits) packed.append(char(acc & 0xFF));
        g.append(char(minCode)).append(blocks(packed));
    }
    return g.append(char(0x3B));
}

QByteArray makeSvg()
{
    return QByteArray(
        "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='80'>"
        "<rect width='120' height='80' fill='#3a7bd5'/></svg>");
}

}  // namespace

class TestImages : public QObject {
    Q_OBJECT
private slots:
    // --- clipboard preference order (SPEC.md §4) -----------------------------
    void pngWinsWhenBothAnImageAndTextAreOffered()
    {
        // Exactly what a browser's "Copy Image" puts on the clipboard.
        QMimeData mime;
        mime.setData(QStringLiteral("image/png"), makePng(20, 10));
        mime.setText(QStringLiteral("https://example.com/cat.png"));

        const auto content = readClipboard(&mime);
        QCOMPARE(content.kind, ClipboardContent::Kind::Image);
        QCOMPARE(content.imageMime, QStringLiteral("image/png"));
        QCOMPARE(content.via, QStringLiteral("image/png"));
    }

    void textIsUsedOnlyWhenNoImageIsOffered()
    {
        QMimeData mime;
        mime.setText(QStringLiteral("systemctl restart nginx"));
        const auto content = readClipboard(&mime);
        QCOMPARE(content.kind, ClipboardContent::Kind::Text);
        QCOMPARE(content.text, QStringLiteral("systemctl restart nginx"));
    }

    void anEmptyClipboardIsIgnoredQuietly()
    {
        QMimeData mime;
        QCOMPARE(readClipboard(&mime).kind, ClipboardContent::Kind::None);
        QCOMPARE(readClipboard(nullptr).kind, ClipboardContent::Kind::None);
    }

    // --- blob store ----------------------------------------------------------
    void storingIsContentAddressedSoDuplicatesCostNothing()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        const QByteArray png = makePng(64, 48);

        const auto first = store.store(png);
        const auto second = store.store(png);

        QVERIFY(first.ok);
        QVERIFY(second.ok);
        QCOMPARE(first.hash, second.hash);
        QCOMPARE(first.size, QSize(64, 48));
        QCOMPARE(store.allStoredFiles().size(), size_t(1));  // one copy on disk
    }

    void jpegIsKeptVerbatimRatherThanInflatedIntoPng()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        const QByteArray jpeg = makeJpeg(200, 150);

        const auto stored = store.store(jpeg);
        QVERIFY(stored.ok);
        QCOMPARE(stored.mime, QStringLiteral("image/jpeg"));
        QCOMPARE(stored.byteSize, qint64(jpeg.size()));      // byte for byte
        QVERIFY(store.pathFor(stored.hash, stored.mime).endsWith(QStringLiteral(".jpg")));
    }

    void nonImageBytesAreRefusedWithAReadableMessage()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        const auto stored = store.store(QByteArray("this is not an image at all"));

        QVERIFY(!stored.ok);
        QVERIFY(!stored.error.isEmpty());
        QVERIFY(!stored.error.contains(QStringLiteral("QImage")));  // no internals leak
        QCOMPARE(store.allStoredFiles().size(), size_t(0));
    }

    void oversizedImagesAreRefused()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        const auto stored = store.store(QByteArray(BlobStore::kMaxBytes + 1, 'x'));
        QVERIFY(!stored.ok);
        QVERIFY(stored.error.contains(QStringLiteral("MB")));
    }

    void noTemporaryFileSurvivesASuccessfulWrite()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        store.store(makePng(10, 10));
        for (const auto& name : store.allStoredFiles())
            QVERIFY(!name.endsWith(QStringLiteral(".tmp")));
    }

    // --- garbage collection --------------------------------------------------
    void orphanBlobsAreReclaimedAndReferencedOnesAreNot()
    {
        GuiFixture f;
        const auto kept = f.blobs.store(makePng(30, 30, Qt::green));
        const auto orphan = f.blobs.store(makePng(40, 40, Qt::yellow));
        QVERIFY(kept.ok && orphan.ok);

        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(kept.hash, 30, 30, kept.byteSize, {}, kept.mime));

        const auto result = reconcileBlobs(f.items, f.blobs);

        QCOMPARE(result.orphansRemoved, 1);
        QVERIFY(result.missingBlobs.empty());
        QVERIFY(f.blobs.exists(kept.hash, kept.mime));
        QVERIFY(!f.blobs.exists(orphan.hash, orphan.mime));
    }

    void interruptedWritesAreCleanedUp()
    {
        GuiFixture f;
        QDir().mkpath(f.blobs.rootDir() + "/ab");
        QFile stray(f.blobs.rootDir() + "/ab/abcdef.png.tmp");
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.write("half a write");
        stray.close();

        const auto result = reconcileBlobs(f.items, f.blobs);
        QCOMPARE(result.temporariesRemoved, 1);
        QVERIFY(!QFile::exists(f.blobs.rootDir() + "/ab/abcdef.png.tmp"));
    }

    void aMissingBlobIsReportedRatherThanIgnored()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(QStringLiteral("deadbeef"), 10, 10, 100));

        const auto result = reconcileBlobs(f.items, f.blobs);
        QCOMPARE(result.missingBlobs.size(), size_t(1));
        QCOMPARE(result.missingBlobs.front(), QStringLiteral("deadbeef"));
    }

    // --- through the UI ------------------------------------------------------
    void pastingAnImageOntoTheStackMakesABufferWithAThumbnail()
    {
        GuiFixture f;
        QMimeData* mime = new QMimeData;
        mime->setData(QStringLiteral("image/png"), makePng(120, 90));
        QApplication::clipboard()->setMimeData(mime);

        f.trigger("pasteAction");

        QCOMPARE(f.buffers.countLive(), 1);
        const auto id = f.buffers.listLive(10).front().id;
        const auto items = f.items.listForBuffer(id);
        QCOMPARE(items.size(), size_t(1));
        QCOMPARE(items[0].type, ItemType::Image);
        QCOMPARE(items[0].width, 120);
        QCOMPARE(items[0].height, 90);
        QCOMPARE(items[0].mime, QStringLiteral("image/png"));

        // And the card can actually draw it.
        QVERIFY(!f.thumbs.forBlob(items[0].blobHash, items[0].mime).isNull());
        const auto row = f.model()->rowForId(id);
        QCOMPARE(f.model()->index(row, 0).data(BufferListModel::ThumbHashRole).toString(),
                 items[0].blobHash);
    }

    void pastingAnImageIntoAnOpenDraftPromotesItRatherThanMakingASecondBuffer()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        QCOMPARE(f.buffers.countLive(), 0);

        QMetaObject::invokeMethod(f.canvas(), "imagePasted", Qt::DirectConnection,
                                  Q_ARG(QByteArray, makePng(64, 64)),
                                  Q_ARG(QString, QStringLiteral("image/png")));

        QCOMPARE(f.buffers.countLive(), 1);
        QCOMPARE(f.model()->rowCount(), 1);
    }

    void aBufferKeepsTextAndImageTogether()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        QTest::keyClicks(f.newTextCard(), "Investigate this bug");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 2000);

        QMetaObject::invokeMethod(f.canvas(), "imagePasted", Qt::DirectConnection,
                                  Q_ARG(QByteArray, makePng(80, 60)),
                                  Q_ARG(QString, QStringLiteral("image/png")));

        const auto id = f.buffers.listLive(10).front().id;
        // Newest first, so the image the user just pasted leads.
        const auto items = f.items.listForBuffer(id);
        QCOMPARE(items.size(), size_t(2));
        QCOMPARE(items[0].type, ItemType::Image);
        QCOMPARE(items[1].type, ItemType::Text);
        QCOMPARE(items[0].position, 1);
        QCOMPARE(items[1].position, 0);

        // The preview reports the count, and still offers a thumbnail.
        const auto row = f.model()->rowForId(id);
        QCOMPARE(f.model()->index(row, 0).data(BufferListModel::SecondaryRole).toString(),
                 QStringLiteral("2 items"));
        QVERIFY(!f.model()->index(row, 0).data(BufferListModel::ThumbHashRole).toString().isEmpty());
    }

    // --- format breadth ------------------------------------------------------
    void theBuildCanDecodeTheFormatsWeAdvertise()
    {
        // Runtime capability, not a hard-coded list: a machine without
        // kimageformats simply has fewer, and Napkin degrades rather than lies.
        QVERIFY(formats::canDecode(QStringLiteral("image/png")));
        QVERIFY(formats::canDecode(QStringLiteral("image/jpeg")));
        QVERIFY(formats::canDecode(QStringLiteral("image/gif")));
        QVERIFY(!formats::pickerFilter().isEmpty());
        QVERIFY(!formats::canDecode(QStringLiteral("application/x-shellscript")));
        QVERIFY(!formats::canDecode(QString()));
    }

    void animationCapableFormatsOutrankPngOnTheClipboard()
    {
        // The bug this test exists for: a source offering both a GIF and a PNG
        // was resolving to the PNG, flattening the animation to one frame.
        QMimeData mime;
        mime.setData(QStringLiteral("image/gif"), makeAnimatedGif());
        mime.setData(QStringLiteral("image/png"), makePng(24, 24));

        const auto content = readClipboard(&mime);
        QCOMPARE(content.kind, ClipboardContent::Kind::Image);
        QCOMPARE(content.imageMime, QStringLiteral("image/gif"));
    }

    void vectorOutranksRaster()
    {
        if (!formats::canDecode(QStringLiteral("image/svg+xml")))
            QSKIP("this build has no SVG plugin");

        QMimeData mime;
        mime.setData(QStringLiteral("image/svg+xml"), makeSvg());
        mime.setData(QStringLiteral("image/png"), makePng(120, 80));

        QCOMPARE(readClipboard(&mime).imageMime, QStringLiteral("image/svg+xml"));
    }

    void anAnimatedGifIsStoredWithItsFramesIntact()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        const auto stored = store.store(makeAnimatedGif());

        QVERIFY2(stored.ok, qPrintable(stored.error));
        QCOMPARE(stored.mime, QStringLiteral("image/gif"));
        QVERIFY(stored.animated);                       // recorded once, at import
        QCOMPARE(stored.size, QSize(24, 24));
        QVERIFY(store.pathFor(stored.hash, stored.mime).endsWith(QStringLiteral(".gif")));

        // Byte for byte: the file on disk is still a playable animation.
        QFile onDisk(store.pathFor(stored.hash, stored.mime));
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QCOMPARE(onDisk.readAll(), makeAnimatedGif());
        QMovie movie(store.pathFor(stored.hash, stored.mime));
        QVERIFY(movie.isValid());
        QCOMPARE(movie.frameCount(), 2);
    }

    void aStillImageIsNotMarkedAnimated()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        QVERIFY(!store.store(makePng(30, 30)).animated);
    }

    void svgIsKeptAsVectorRatherThanRasterised()
    {
        if (!formats::canDecode(QStringLiteral("image/svg+xml")))
            QSKIP("this build has no SVG plugin");

        QTemporaryDir dir;
        BlobStore store(dir.path());
        const auto stored = store.store(makeSvg());

        QVERIFY2(stored.ok, qPrintable(stored.error));
        QVERIFY(stored.mime.startsWith(QStringLiteral("image/svg")));
        QCOMPARE(stored.byteSize, qint64(makeSvg().size()));   // still the XML
    }

    void theAnimatedFlagSurvivesTheDatabase()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makeAnimatedGif());
        QVERIFY(stored.ok);

        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 24, 24, stored.byteSize,
                                               QStringLiteral("loop.gif"), stored.mime, true));
        f.model()->reload();

        QVERIFY(f.items.listForBuffer(id).front().animated);
        const int row = f.model()->rowForId(id);
        QVERIFY(f.model()->index(row, 0).data(BufferListModel::ThumbAnimatedRole).toBool());
    }

    void anUnreadableFormatIsRefusedRatherThanStoredAsGarbage()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        const auto stored = store.store(QByteArray("\x00\x01\x02 not any image", 24));
        QVERIFY(!stored.ok);
        QCOMPARE(store.allStoredFiles().size(), size_t(0));
    }

    // --- empty trash ---------------------------------------------------------
    void emptyingTheTrashReclaimsTheBlobsItHeld()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(50, 50));
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 50, 50, stored.byteSize, {}, stored.mime));
        f.model()->reload();

        f.service.trash(id);
        QVERIFY(f.blobs.exists(stored.hash, stored.mime));  // trash is not deletion

        QCOMPARE(f.service.emptyTrash(), 1);
        reconcileBlobs(f.items, f.blobs);

        QCOMPARE(f.buffers.countTrash(), 0);
        QVERIFY(!f.blobs.exists(stored.hash, stored.mime));
    }

    void emptyingTheTrashLeavesLiveBuffersAlone()
    {
        GuiFixture f;
        const auto live = f.seed("still here");
        const auto doomed = f.seed("not for long");
        f.service.trash(doomed);

        QCOMPARE(f.service.emptyTrash(), 1);
        QCOMPARE(f.buffers.countLive(), 1);
        QVERIFY(f.buffers.find(live).has_value());
        QVERIFY(!f.buffers.find(doomed).has_value());
    }

    void emptyingTheTrashCannotTakeAKeptBuffer()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.buffers.setKept(id, true);
        // Force it into the trash with the keep intact, simulating a bug.
        f.db.exec(QString("UPDATE buffers SET deleted_at = 1 WHERE id = %1").arg(id)
                      .toUtf8().constData());

        QCOMPARE(f.service.emptyTrash(), 0);   // skipped, not destroyed
        QVERIFY(f.buffers.find(id).has_value());
    }

    // --- previews are made off the UI thread ----------------------------------
    // Decoding a board preview cost ~100 ms on the UI thread, per image, and a
    // board of 500 froze for 14 s on its first scroll. The card must come up
    // without its picture and have it delivered.
    void anImageCardAppearsAtOnceAndItsPictureArrivesAfter()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(800, 600));
        QVERIFY(stored.ok);
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 800, 600, stored.byteSize,
                                               {}, stored.mime));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<ImageItemCard*>().first();
        auto* view = card->findChildren<QLabel*>().first();
        // Nothing has run the event loop, so nothing can have been delivered:
        // if the picture is already here, it was decoded on this thread.
        QVERIFY2(card->isLoadingPreview(), "the preview was made synchronously");
        QVERIFY(view->text().isEmpty());   // and the wait is not dressed up as an error
        QVERIFY(!f.thumbs.isIdle());
        // The placeholder already has the picture's shape, so the caption below
        // it does not move when the picture lands.
        const QSize placeholder = view->pixmap().size();
        QVERIFY(!placeholder.isEmpty());

        QTRY_VERIFY_WITH_TIMEOUT(!card->isLoadingPreview(), 5000);
        QVERIFY(!view->pixmap().isNull());
        QCOMPARE(view->pixmap().size(), placeholder);
    }

    void aPreviewThatCannotBeMadeSaysSoWhenTheWorkerGivesUp()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(200, 100));
        QVERIFY(stored.ok);
        // The file is there, and is not an image any more.
        {
            QFile file(f.blobs.pathFor(stored.hash, stored.mime));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("not a png at all");
        }
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 200, 100, stored.byteSize,
                                               {}, stored.mime));
        f.model()->reload();
        f.select(id);

        auto* view = f.canvas()->findChildren<ImageItemCard*>().first()
                         ->findChildren<QLabel*>().first();
        QTRY_VERIFY_WITH_TIMEOUT(!view->text().isEmpty(), 5000);
        QVERIFY(view->text().contains(QStringLiteral("too large")));
        QVERIFY(view->pixmap().isNull());
    }

    void aMissingImageSaysSoWithoutWaitingForAnything()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(200, 100));
        QVERIFY(stored.ok);
        QVERIFY(QFile::remove(f.blobs.pathFor(stored.hash, stored.mime)));
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 200, 100, stored.byteSize,
                                               {}, stored.mime));
        f.model()->reload();
        f.select(id);

        auto* view = f.canvas()->findChildren<ImageItemCard*>().first()
                         ->findChildren<QLabel*>().first();
        QVERIFY(view->text().contains(QStringLiteral("no longer on disk")));
    }

    void theListPaintsWithoutDecodingAndRepaintsWhenThePictureIsReady()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(640, 480));
        QVERIFY(stored.ok);
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 640, 480, stored.byteSize,
                                               {}, stored.mime));
        f.model()->reload();

        // A paint of the list asks for the thumbnail and must not wait for it.
        f.view()->viewport()->grab();
        QVERIFY2(!f.thumbs.isIdle(), "painting the list decoded the image itself");
        f.thumbs.waitForIdle();
        QPixmap thumb;
        QCOMPARE(f.thumbs.request(stored.hash, stored.mime, BufferCardDelegate::kThumbSize * 2,
                                  &thumb), Thumbnailer::State::Ready);
        QVERIFY(!thumb.isNull());
    }

    void aSmallImageIsNotBlownUpToMakeItsPreview()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(40, 30));
        QVERIFY(stored.ok);
        const QPixmap preview = f.thumbs.forBlob(stored.hash, stored.mime, 920);
        QCOMPARE(preview.size(), QSize(40, 30));
        const QPixmap large = f.thumbs.forBlob(f.blobs.store(makePng(2000, 1000)).hash,
                                               QStringLiteral("image/png"), 920);
        QCOMPARE(large.size(), QSize(920, 460));
    }

    // --- the sweep runs off the UI thread -------------------------------------
    void emptyingTheTrashReclaimsInTheBackground()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(300, 200));
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 300, 200, stored.byteSize,
                                               {}, stored.mime));
        QVERIFY(!f.thumbs.forBlob(stored.hash, stored.mime).isNull());   // a thumbnail on disk
        f.service.trash(id);
        f.model()->reload();

        f.window.emptyTrashForTest();
        // Returned without having walked anything: the deciding half runs on
        // this thread, and nothing has let it run yet.
        QVERIFY(f.window.sweeperForTest()->isRunning());
        QVERIFY(f.blobs.exists(stored.hash, stored.mime));

        f.window.sweeperForTest()->waitForIdle();
        QVERIFY(!f.blobs.exists(stored.hash, stored.mime));
        QCOMPARE(f.window.sweeperForTest()->lastResult().thumbnailsRemoved, 1);
    }

    // Content addressing means pasting an image that is already on disk reuses
    // the file. If that file was an orphan when the walk listed it, a sweep
    // deciding from the walk's snapshot would delete a picture that has just
    // been pasted.
    void aPasteDuringTheSweepKeepsItsPicture()
    {
        GuiFixture f;
        const auto orphan = f.blobs.store(makePng(64, 64, Qt::green));   // no row: an orphan
        auto* sweeper = f.window.sweeperForTest();
        sweeper->start();
        // Sleep WITHOUT running the event loop: the walk is certainly over,
        // and its result cannot have been acted on yet. A walk that deleted on
        // its own thread has already done it.
        QThread::msleep(300);
        QVERIFY2(f.blobs.exists(orphan.hash, orphan.mime),
                 "the walking thread deleted files itself");

        const auto id = f.buffers.create();
        const auto again = f.blobs.store(makePng(64, 64, Qt::green));   // the same bytes
        QCOMPARE(again.hash, orphan.hash);
        f.service.appendTo(id, Item::makeImage(again.hash, 64, 64, again.byteSize,
                                               {}, again.mime));

        sweeper->waitForIdle();
        QVERIFY2(f.blobs.exists(orphan.hash, orphan.mime),
                 "the sweep deleted an image that was pasted while it was running");
    }

    // The protection an undo offer gives is read when the sweep DECIDES, not
    // when it starts. Driven on a sweeper of its own: in the window, deleting
    // an image moves it to the trash, whose row keeps the blob alive anyway,
    // so no window path reaches this with a blob today — which is exactly when
    // a contract like this is quietly broken.
    void protectionGivenWhileTheSweepRunsStillCounts()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(40, 30));   // no row
        QSet<QString> held;
        BlobSweeper sweeper(f.items, f.blobs, f.thumbsDir());
        sweeper.setProtectedHashes([&held] { return held; });
        sweeper.start();
        QThread::msleep(300);   // the walk is over; see aPasteDuringTheSweepKeepsItsPicture

        held.insert(stored.hash);   // an offer appears, holding it
        sweeper.waitForIdle();
        QVERIFY2(f.blobs.exists(stored.hash, stored.mime),
                 "protection given while the sweep ran was not honoured");

        held.clear();               // the offer goes; the next sweep may take it
        sweeper.start();
        sweeper.waitForIdle();
        QVERIFY(!f.blobs.exists(stored.hash, stored.mime));
    }

    void theWindowSweepsItsOwnThumbnailsNotTheProfiles()
    {
        GuiFixture f;
        // A rendering of something no row references, in the fixture's cache.
        const QString stale = f.thumbsDir() + QStringLiteral("/ab/abcdef_96.png");
        QDir().mkpath(QFileInfo(stale).absolutePath());
        QVERIFY(makePngImage().save(stale, "PNG"));

        f.window.emptyTrashForTest();
        f.window.sweeperForTest()->waitForIdle();
        QVERIFY2(!QFile::exists(stale),
                 "the window swept some other directory — the real profile's");
    }

    // --- the lightbox pages through the napkin --------------------------------
    void theLightboxPagesThroughTheNapkinsImagesWithTheArrowKeys()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        QStringList names;
        for (int i = 0; i < 3; ++i) {
            const auto stored = f.blobs.store(makePng(100 + i, 80, QColor::fromHsv(i * 90, 200, 200)));
            const QString name = QStringLiteral("shot-%1").arg(i);
            names << name;
            f.service.appendTo(id, Item::makeImage(stored.hash, 100 + i, 80, stored.byteSize,
                                                   name, stored.mime));
            if (i == 1)   // text between images is skipped, not shown blank
                f.service.appendTo(id, Item::makeText(QStringLiteral("between")));
        }
        f.model()->reload();
        f.select(id);

        // The board is newest first: shot-2, shot-1, shot-0.
        const auto order = f.canvas()->itemOrder();
        ItemId middle = kNoItem;
        for (ItemId each : order)
            if (const auto item = f.items.find(each); item && item->sourceName == names[1])
                middle = each;
        QVERIFY(middle != kNoItem);

        QStringList seen;
        int countSeen = 0;
        QTimer::singleShot(0, [&] {
            auto* box = f.window.lightboxForTest();
            if (!box) return;
            countSeen = box->count();
            auto title = [box] { return box->windowTitle().section(QStringLiteral(" — "), 0, 0); };
            seen << title();
            QTest::keyClick(box, Qt::Key_Right); seen << title();
            QTest::keyClick(box, Qt::Key_Right); seen << title();   // the end: stays
            QTest::keyClick(box, Qt::Key_Home);  seen << title();
            QTest::keyClick(box, Qt::Key_Left);  seen << title();   // the start: stays
            box->accept();
        });
        QMetaObject::invokeMethod(f.canvas(), "imageActivated", Qt::DirectConnection,
                                  Q_ARG(napkin::ItemId, middle));

        QCOMPARE(countSeen, 3);
        QCOMPARE(seen, (QStringList{names[1], names[0], names[0], names[2], names[2]}));
    }

    // request() writes its pixmap only on success, and the delegate started
    // from the playing animation's frame — so a missing thumbnail drew some
    // other row's GIF instead of its "?" (independent review).
    void aMissingThumbnailNeverBorrowsTheAnimationPlayingElsewhere()
    {
        GuiFixture f;
        const auto gone = f.blobs.store(makePng(60, 60, Qt::blue));
        QVERIFY(QFile::remove(f.blobs.pathFor(gone.hash, gone.mime)));
        const auto a = f.buffers.create();
        f.service.appendTo(a, Item::makeImage(gone.hash, 60, 60, gone.byteSize, {}, gone.mime));
        const auto b = f.buffers.create();
        f.service.appendTo(b, Item::makeText(QStringLiteral("other")));
        f.model()->reload();
        f.view()->viewport()->grab();
        f.thumbs.waitForIdle();   // A's thumbnail has now failed

        auto* delegate = f.view()->findChild<BufferCardDelegate*>();
        QPixmap magenta(64, 64);
        magenta.fill(Qt::magenta);
        delegate->setAnimationFrame(f.model()->rowForId(b), magenta);

        const QRect rowA = f.view()->visualRect(f.model()->index(f.model()->rowForId(a), 0));
        const QImage shot = f.view()->viewport()->grab(rowA).toImage();
        int borrowed = 0;
        for (int y = 0; y < shot.height(); ++y)
            for (int x = 0; x < shot.width(); ++x)
                if (shot.pixelColor(x, y) == QColor(Qt::magenta)) ++borrowed;
        QCOMPARE(borrowed, 0);
    }

    // 0.1.7 stored small images scaled UP, under the same file name.
    void aPreviewUpscaledByAnOlderVersionIsRemade()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(200, 140));
        const QString old = f.thumbsDir() + QStringLiteral("/%1/%2_920.png")
                                                .arg(stored.hash.left(2), stored.hash);
        QDir().mkpath(QFileInfo(old).absolutePath());
        QImage blown(920, 644, QImage::Format_RGB32);
        blown.fill(Qt::red);
        QVERIFY(blown.save(old, "PNG"));

        QCOMPARE(f.thumbs.forBlob(stored.hash, stored.mime, 920).size(), QSize(200, 140));
        QCOMPARE(QImage(old).size(), QSize(200, 140));   // and the file was replaced
    }

    // The half of the list test that it used to only claim: a delivered
    // thumbnail makes the list paint again.
    void aDeliveredThumbnailRepaintsTheList()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(makePng(640, 480));
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 640, 480, stored.byteSize,
                                               {}, stored.mime));
        f.model()->reload();
        QApplication::processEvents();
        f.view()->viewport()->repaint();   // asks for the thumbnail
        QVERIFY(!f.thumbs.isIdle());
        QApplication::processEvents();     // flush anything already pending

        struct PaintCounter : QObject {
            int paints = 0;
            bool eventFilter(QObject*, QEvent* e) override
            {
                if (e->type() == QEvent::Paint) ++paints;
                return false;
            }
        } counter;
        f.view()->viewport()->installEventFilter(&counter);
        f.thumbs.waitForIdle();            // delivers, and emits ready()
        QTRY_VERIFY_WITH_TIMEOUT(counter.paints > 0, 2000);
        f.view()->viewport()->removeEventFilter(&counter);
    }
};

QTEST_MAIN(TestImages)
#include "test_images.moc"
