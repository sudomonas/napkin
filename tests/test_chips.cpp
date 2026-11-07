#include "../src/ui/LinkChip.h"
#include "GuiFixture.h"

#include <QPushButton>
#include <QLabel>
#include <QtTest>

using namespace napkin;

// SPEC.md §3 and §18.3: paste a URL, it renders as a link chip. The chip is
// derived presentation, so the tests that matter are the ones proving the item
// is still an ordinary, editable, searchable text item underneath.
class TestChips : public QObject {
    Q_OBJECT
private:
    static TextItemCard* onlyTextCard(GuiFixture& f)
    {
        const auto cards = f.canvas()->findChildren<TextItemCard*>();
        return cards.isEmpty() ? nullptr : cards.first();
    }

private slots:
    void aLoneUrlRendersAsAChip()
    {
        GuiFixture f;
        const auto id = f.seed("https://example.org/a/path");
        f.select(id);

        auto* card = onlyTextCard(f);
        QVERIFY(card);
        QVERIFY(card->showingChip());
        QCOMPARE(card->linkUrl(), QStringLiteral("https://example.org/a/path"));

        auto* chip = card->findChild<LinkChip*>();
        QVERIFY(chip);
        // The address in full for a screen reader; the visual chip is a summary.
        QCOMPARE(chip->accessibleDescription(), QStringLiteral("https://example.org/a/path"));
    }

    void proseIsNotAChip()
    {
        GuiFixture f;
        const auto id = f.seed("see https://example.org for the details");
        f.select(id);
        QVERIFY(!onlyTextCard(f)->showingChip());
    }

    void theChipStepsAsideForEditing()
    {
        GuiFixture f;
        const auto id = f.seed("https://example.org/");
        f.select(id);

        auto* card = onlyTextCard(f);
        QVERIFY(card->showingChip());

        card->beginEditing();
        // Nothing may stand between the user and the text. The item is text.
        // hasEditFocus() is editing MODE: real window focus is not something a
        // headless test can grant, so asserting it here would only be asserting
        // the platform plugin.
        QVERIFY(!card->showingChip());
        QVERIFY(card->hasEditFocus());
        // The editor has to be the widget that is actually up, or the caret
        // would be going to something hidden.
        QVERIFY(!card->findChild<QPlainTextEdit*>()->isHidden());
        QVERIFY(card->findChild<LinkChip*>()->isHidden());

        card->endEditing();
        QVERIFY(card->showingChip());
    }

    void editingCanCreateAndDestroyAChip()
    {
        GuiFixture f;
        const auto id = f.seed("just a note");
        f.select(id);

        auto* card = onlyTextCard(f);
        QVERIFY(!card->showingChip());

        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral("https://example.org/"));
        card->endEditing();
        QVERIFY(card->showingChip());

        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral("no longer a link"));
        card->endEditing();
        QVERIFY(!card->showingChip());
    }

    void aChipCardIsTallEnoughToDrawAChipIn()
    {
        // The board measures from item data and never constructs a card, so it
        // has to know the chip rule too. It did not, and every chip was drawn
        // into a card sized for one line of text — the labels overlapped and
        // the Open button was clipped to a sliver.
        GuiFixture f;
        const auto id = f.seed("https://example.org/");
        f.select(id);

        auto* card = onlyTextCard(f);
        QVERIFY2(card->height() >= LinkChip::preferredHeight(card->font()),
                 qPrintable(QStringLiteral("card is %1px, chip needs %2px")
                                .arg(card->height()).arg(LinkChip::preferredHeight(card->font()))));
        auto* open = card->findChild<QPushButton*>(QStringLiteral("linkChipOpen"));
        QVERIFY(open);
        QVERIFY(open->height() >= 20);
    }

    void aChippedUrlIsStillFoundBySearch()
    {
        // Derived presentation must not cost the text index: the chip is how
        // the item is drawn, not what it is.
        GuiFixture f;
        f.seed("https://example.org/deployment-notes");
        f.seed("something else entirely");

        f.model()->setQuery(QStringLiteral("deployment"));
        QCOMPARE(f.model()->rowCount(), 1);
    }

    void onlyHttpUrlsBecomeChips()
    {
        GuiFixture f;
        const auto id = f.seed("file:///etc/passwd");
        f.select(id);
        QVERIFY(!onlyTextCard(f)->showingChip());
    }

    void aLongHostIsElidedNotCutMidLetter()
    {
        // Usability test: "www.example.c", clipped by its label.
        LinkChip chip;
        chip.setUrl(QStringLiteral("https://www.build-artifacts.ci.eu-west-2.internal.example.com/a/b"));
        chip.resize(250, chip.sizeHint().height());   // the narrowest a card makes it
        chip.show();
        QCoreApplication::processEvents();
        auto* host = chip.findChildren<QLabel*>().first();
        const QString shown = host->text();
        QVERIFY2(!shown.startsWith(QStringLiteral("www.")), qPrintable(shown));
        QVERIFY2(shown.startsWith(QChar(0x2026)) || shown == QStringLiteral("build-artifacts.ci.eu-west-2.internal.example.com"),
                 qPrintable(shown));
        QVERIFY2(QFontMetrics(host->font()).horizontalAdvance(shown) <= host->width(), qPrintable(shown));
        // Whose site it is survives whole — whenever that can fit at all. The
        // test cannot assume a width: CI's font is wider than the dev
        // machine's, and there "…example.com" does not fit in 250px, where the
        // honest fallback is to cut inside the name.
        const QString domain = QString(QChar(0x2026)) + QStringLiteral("example.com");
        if (QFontMetrics(host->font()).horizontalAdvance(domain) <= host->width())
            QVERIFY2(shown.endsWith(QStringLiteral(".example.com")) || shown == domain, qPrintable(shown));
    }

};

QTEST_MAIN(TestChips)
#include "test_chips.moc"
