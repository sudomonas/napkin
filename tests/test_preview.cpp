#include "../src/domain/Preview.h"
#include "../src/domain/TimeFormat.h"
#include <QtTest>

using namespace napkin;

// Preview text is derived, never entered (SPEC.md §1). These cases pin down
// what a card says for each shape of buffer.
class TestPreview : public QObject {
    Q_OBJECT
private slots:
    void emptyBufferHasNoPreview()
    {
        const auto p = derivePreview({}, 0, 0);
        QVERIFY(p.isEmpty());
        QCOMPARE(p.itemCount, 0);
    }

    void singleTextUsesItsFirstLine()
    {
        const auto p = derivePreview({Item::makeText("systemctl restart nginx")}, 1, 0);
        QCOMPARE(p.primary, QStringLiteral("systemctl restart nginx"));
        QVERIFY(p.secondary.isEmpty());
    }

    void leadingBlankLinesAreSkipped()
    {
        const auto p = derivePreview({Item::makeText("\n\n   \nreal content\nmore")}, 1, 0);
        QCOMPARE(p.primary, QStringLiteral("real content"));
        QCOMPARE(p.secondary, QStringLiteral("more"));
    }

    void secondLineBecomesTheDetail()
    {
        const auto p = derivePreview(
            {Item::makeText("sudo pacman -Syu\nNeed to check whether this breaks KDE.")}, 1, 0);
        QCOMPARE(p.primary, QStringLiteral("sudo pacman -Syu"));
        QCOMPARE(p.secondary, QStringLiteral("Need to check whether this breaks KDE."));
    }

    void pastedImageHasNoFilenameSoItIsCalledImage()
    {
        // Not "Screenshot": a pasted photo is not one, and naming it so is
        // interpreting content (SPEC.md §1).
        const auto p = derivePreview({Item::makeImage("abc", 1920, 1080, 4096)}, 1, 1);
        QCOMPARE(p.primary, QStringLiteral("Image"));
        QCOMPARE(p.secondary, QStringLiteral("1920 × 1080"));
        QVERIFY(p.hasImage());
    }

    void animportedImageKeepsItsName()
    {
        const auto p = derivePreview({Item::makeImage("abc", 800, 600, 4096, "diagram.png")}, 1, 1);
        QCOMPARE(p.primary, QStringLiteral("diagram.png"));
    }

    void multipleItemsReportTheCountInsteadOfDetail()
    {
        const auto p = derivePreview(
            {Item::makeText("Investigate this bug"), Item::makeImage("h", 10, 10, 1)}, 3, 1);
        QCOMPARE(p.primary, QStringLiteral("Investigate this bug"));
        QCOMPARE(p.secondary, QStringLiteral("3 items"));  // more useful than line 2
        QCOMPARE(p.itemCount, 3);
        QVERIFY(p.hasImage());
    }

    void whitespaceOnlyTextYieldsNothing()
    {
        const auto p = derivePreview({Item::makeText("   \n\t\n  ")}, 1, 0);
        QVERIFY(p.primary.isEmpty());
    }

    void severalImagesYieldOneThumbnailAndACount()
    {
        std::vector<Item> head;
        for (int i = 0; i < 5; ++i)
            head.push_back(Item::makeImage(QStringLiteral("hash%1").arg(i), 100, 100, 10));

        const auto p = derivePreview(head, 5, 5);
        QCOMPARE(p.imageCount, 5);
        // One thumbnail, not three: the row only has to say "there are pictures
        // in here", and the canvas beside it shows every one of them.
        QCOMPARE(int(p.thumbs.size()), kMaxCardThumbs);
        QVERIFY(!p.thumbs.empty());
        QCOMPARE(p.thumbs.front().hash, QStringLiteral("hash0"));
        QCOMPARE(p.primary, QStringLiteral("5 images"));
    }

    void textLeadsEvenWhenImagesFollow()
    {
        const auto p = derivePreview({Item::makeText("Investigate this bug"),
                                      Item::makeImage("a", 10, 10, 1),
                                      Item::makeImage("b", 10, 10, 1)}, 3, 2);
        QCOMPARE(p.primary, QStringLiteral("Investigate this bug"));
        QCOMPARE(p.secondary, QStringLiteral("3 items"));
        QCOMPARE(int(p.thumbs.size()), 1);
    }

    void byteSizes()
    {
        QCOMPARE(formatBytes(512), QStringLiteral("512 B"));
        QCOMPARE(formatBytes(2048), QStringLiteral("2 KB"));
        QCOMPARE(formatBytes(4509715660LL), QStringLiteral("4.2 GB"));
    }

    // --- relative time -------------------------------------------------------
    void relativeTimeBuckets()
    {
        constexpr qint64 s = 1000, m = 60 * s, h = 60 * m, d = 24 * h;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();

        QCOMPARE(relativeTime(now, now), QStringLiteral("just now"));
        QCOMPARE(relativeTime(now - 30 * s, now), QStringLiteral("just now"));
        QCOMPARE(relativeTime(now - 1 * m, now), QStringLiteral("a minute ago"));
        QCOMPARE(relativeTime(now - 2 * m, now), QStringLiteral("2 minutes ago"));

        // Beyond an hour the label depends on the calendar, not the elapsed
        // time, so assert it is present and not a fallback rather than exact.
        QVERIFY(!relativeTime(now - 3 * h, now).isEmpty());
        QVERIFY(!relativeTime(now - 400 * d, now).isEmpty());
    }

    void clockSkewNeverReadsAsTheFuture()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QCOMPARE(relativeTime(now + 5 * 60'000, now), QStringLiteral("just now"));
    }

    // Second usability test: a napkin that began with a picture or a link was
    // titled "Image" or by its raw URL although a note on it said what it was.
    void aNoteTitlesTheNapkinEvenWhenAPictureCameFirst()
    {
        const auto p = derivePreview({Item::makeImage("abc", 640, 420, 9000),
                                      Item::makeText("Architecture sketch from Monday standup")}, 2, 1);
        QCOMPARE(p.primary, QStringLiteral("Architecture sketch from Monday standup"));
    }

    void aNoteTitlesTheNapkinOverALink()
    {
        const auto p = derivePreview({Item::makeText("https://www.example.com/flight-booking/confirmation?id=AB123"),
                                      Item::makeText("Packing list for the trip")}, 2, 0);
        QCOMPARE(p.primary, QStringLiteral("Packing list for the trip"));
    }

    void aLinkOnItsOwnIsShownShortNotRaw()
    {
        const auto p = derivePreview({Item::makeText("https://www.example.com/flight-booking")}, 1, 0);
        QVERIFY2(!p.primary.startsWith(QStringLiteral("https://")), qPrintable(p.primary));
        QVERIFY(p.primary.contains(QStringLiteral("example.com")));
    }


    // --- the latest addition (test user, 2026-10-06) --------------------------
    static Item withId(Item i, ItemId id) { i.id = id; return i; }

    void theLatestAdditionIsShownBesideTheTitle()
    {
        const Item title = withId(Item::makeText("Groceries"), 1);
        const Item milk = withId(Item::makeText("milk\nsemi-skimmed"), 3);
        const auto p = derivePreview({title}, 3, 0, {milk, withId(Item::makeText("eggs"), 2)});
        QCOMPARE(p.primary, QStringLiteral("Groceries"));
        QCOMPARE(p.latest, QStringLiteral("milk"));
    }

    void theLatestIsNotTheTitleTwice()
    {
        // The title note was the last thing edited: say what came before it.
        const Item title = withId(Item::makeText("Groceries"), 1);
        const auto p = derivePreview({title}, 2, 0, {title, withId(Item::makeText("eggs"), 2)});
        QCOMPARE(p.latest, QStringLiteral("eggs"));
    }

    void aSingleItemHasNoLatestLine()
    {
        const Item only = withId(Item::makeText("alone"), 1);
        QVERIFY(derivePreview({only}, 1, 0, {only}).latest.isEmpty());
    }

    void aLatestLinkOrImageReadsAsOne()
    {
        const Item title = withId(Item::makeText("Research"), 1);
        const Item link = withId(Item::makeText("https://www.example.org/paper?id=4"), 2);
        QVERIFY(derivePreview({title}, 2, 0, {link}).latest.startsWith(QStringLiteral("www.example.org/paper")));
        Item shot = withId(Item::makeImage("h", 10, 10, 1), 3);
        QCOMPARE(derivePreview({title}, 2, 1, {shot}).latest, QStringLiteral("Image"));
        shot.sourceName = QStringLiteral("chart.png");
        QCOMPARE(derivePreview({title}, 2, 1, {shot}).latest, QStringLiteral("chart.png"));
    }
};

QTEST_APPLESS_MAIN(TestPreview)
#include "test_preview.moc"
