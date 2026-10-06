#include "../src/ui/CardFooter.h"
#include "../src/ui/ItemCanvas.h"
#include "../src/ui/LinkChip.h"
#include "GuiFixture.h"

#include <QAbstractButton>
#include <QLineEdit>
#include <QMenuBar>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace napkin;

// SPEC.md §14. Keyboard for every action, visible focus, screen-reader labels,
// predictable tab order — and never meaning carried by colour alone.
//
// These are written as sweeps over the real widget tree rather than as a list
// of named widgets, so a control added later is covered by them without anyone
// remembering to come back here.
class TestAccessibility : public QObject {
    Q_OBJECT
private:
    // What a screen reader would announce for a widget: Qt falls back to the
    // button's own text when no accessible name is set, and that is usually the
    // right answer — the failure is a control with neither.
    static QString announced(QWidget* w)
    {
        if (!w->accessibleName().isEmpty()) return w->accessibleName();
        if (auto* button = qobject_cast<QAbstractButton*>(w))
            if (!button->text().isEmpty()) return button->text();
        return {};
    }

private slots:
    void everyControlAnnouncesItself()
    {
        GuiFixture f;
        f.seed("a note to look at");

        QStringList silent;
        for (QWidget* w : f.window.findChildren<QWidget*>()) {
            if (w->isHidden()) continue;
            const bool interactive = qobject_cast<QAbstractButton*>(w)
                                     || qobject_cast<QLineEdit*>(w);
            if (!interactive) continue;
            if (announced(w).isEmpty())
                silent << QStringLiteral("%1(%2)")
                              .arg(QString::fromLatin1(w->metaObject()->className()),
                                   w->objectName());
        }
        QVERIFY2(silent.isEmpty(), qPrintable(QStringLiteral("unlabelled: ")
                                                  + silent.join(QStringLiteral(", "))));
    }

    // With no menu bar there is no Alt to open one, so the menu button needs a
    // key of its own (F10, as GNOME and KDE use) and a place in the Tab order.
    // Ctrl+F reached the search field and a click did not: it had been left
    // with no focus policy, and setFocus() ignores the policy while a mouse
    // press honours it. Both ways in must work.
    void theSearchFieldTakesFocusFromAClickAndFromTab()
    {
        GuiFixture f;
        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        QVERIFY(field);
        QVERIFY2(field->focusPolicy() & Qt::ClickFocus, "a click cannot focus the search field");
        QVERIFY2(field->focusPolicy() & Qt::TabFocus, "Tab cannot reach the search field");
    }

    void everyMenuActionIsReachableByKeyboard()
    {
        GuiFixture f;
        auto* button = f.window.findChild<QToolButton*>(QStringLiteral("overflowButton"));
        QVERIFY(button && button->menu());
        QVERIFY2(button->focusPolicy() & Qt::TabFocus, "the menu button cannot be tabbed to");

        auto* open = f.window.findChild<QAction*>(QStringLiteral("openMenuAction"));
        QVERIFY(open);
        QCOMPARE(open->shortcut(), QKeySequence(Qt::Key_F10));
        QVERIFY2(f.window.actions().contains(open), "F10 is not bound to the window");

        for (QAction* item : button->menu()->actions()) {
            if (item->isSeparator()) continue;
            QVERIFY2(!item->text().isEmpty(), "a nameless action in the menu");
        }
        QTimer::singleShot(0, button->menu(), [menu = button->menu()] { menu->hide(); });
        open->trigger();
    }

    void theBufferListSpeaksItsRows()
    {
        // The list is the primary navigation surface. Drawn by a custom
        // delegate, so nothing about a row reaches a screen reader unless the
        // model says it: without this a buffer announces as an empty row.
        GuiFixture f;
        const auto id = f.seed("the quarterly numbers");
        const int row = f.model()->rowForId(id);

        const QString spoken =
            f.model()->data(f.model()->index(row, 0), Qt::AccessibleTextRole).toString();
        QVERIFY2(!spoken.isEmpty(), "a row announces as nothing at all");
        QVERIFY(spoken.contains(QStringLiteral("quarterly numbers")));
    }

    void pinnedAndKeptAreAnnouncedNotOnlyTinted()
    {
        // §14, in as many words: "Never encode meaning in color alone. Pinned
        // and kept states carry an icon and an accessible label, not just a
        // tint."
        GuiFixture f;
        const auto id = f.seed("something worth keeping");
        const int row = f.model()->rowForId(id);
        auto spoken = [&] {
            return f.model()->data(f.model()->index(row, 0), Qt::AccessibleTextRole).toString();
        };

        const QString plain = spoken();
        QVERIFY(!plain.contains(QStringLiteral("Pinned")));

        f.window.togglePin(row);
        QVERIFY2(spoken().contains(QStringLiteral("Pinned")), qPrintable(spoken()));

        f.window.toggleKeep(f.model()->rowForId(id));
        QVERIFY2(spoken().contains(QStringLiteral("Kept")), qPrintable(spoken()));
    }

    void nothingClipsWhenTheDesktopFontIsLarge()
    {
        // §14 asks for sensible text scaling. Several constants in Tokens.h and
        // in the chip were pixel counts that happened to fit at the default
        // font: at 200% the card's chrome estimate was smaller than the footer
        // it had to hold, so the content area shrank and the chip clipped its
        // own descenders.
        const QFont original = QApplication::font();
        QFont big = original;
        big.setPointSizeF(original.pointSizeF() * 2.0);
        QApplication::setFont(big);

        {
            GuiFixture f;
            const auto id = f.seed("https://example.org/a/path");
            f.select(id);

            auto* card = f.canvas()->findChildren<TextItemCard*>().value(0);
            QVERIFY(card);
            QVERIFY(card->showingChip());

            // The footer grew with the font rather than staying at its
            // default-font constant...
            auto* footer = card->findChild<CardFooter*>();
            QVERIFY(footer);
            QVERIFY2(footer->height() >= QFontMetrics(big).height(),
                     qPrintable(QStringLiteral("footer %1px, text %2px")
                                    .arg(footer->height()).arg(QFontMetrics(big).height())));

            // ...and the card is still tall enough for the chip plus that
            // larger chrome, which is the part that was actually broken.
            QVERIFY2(card->height() >= LinkChip::preferredHeight() + footer->height(),
                     qPrintable(QStringLiteral("card %1px, chip %2px, footer %3px")
                                    .arg(card->height())
                                    .arg(LinkChip::preferredHeight())
                                    .arg(footer->height())));
        }

        QApplication::setFont(original);
    }

    void changingTheFontOnARunningWindowReflowsTheBoard()
    {
        // Settings ▸ Appearance can change the typeface and size of a window
        // that is already open. The board caches measurements keyed on the item
        // and the column width — the font is not in that key, so without an
        // explicit invalidation every card kept the height its old face needed.
        const QFont original = QApplication::font();

        GuiFixture f;
        const auto id = f.seed("A shipping address\n12 Mill Lane\nBristol BS1 4AA");
        f.select(id);

        auto tallest = [&] {
            int h = 0;
            for (auto* card : f.canvas()->findChildren<ItemCard*>())
                h = std::max(h, card->height());
            return h;
        };
        const int before = tallest();
        QVERIFY(before > 0);

        QFont big = original;
        big.setPointSizeF(original.pointSizeF() * 2.0);
        QApplication::setFont(big);
        QCoreApplication::processEvents();

        const int after = tallest();
        QApplication::setFont(original);
        QCoreApplication::processEvents();

        QVERIFY2(after > before,
                 qPrintable(QStringLiteral("card was %1px at 1x and %2px at 2x")
                                .arg(before).arg(after)));
    }

    void theBoardAndTheListSayWhatTheyAre()
    {
        GuiFixture f;
        f.seed("anything");
        QVERIFY(!f.view()->accessibleName().isEmpty());
        QVERIFY(!f.canvas()->accessibleName().isEmpty());
    }
};

QTEST_MAIN(TestAccessibility)
#include "test_accessibility.moc"
