#include "../src/ui/SettingsDialog.h"
#include "../src/ui/Tokens.h"
#include "../src/ui/ItemCanvas.h"
#include "../src/ui/BufferCardDelegate.h"
#include "../src/ui/BufferListModel.h"
#include "GuiFixture.h"

#include <QLabel>
#include <QAbstractButton>
#include <QTimer>
#include <QImageReader>
#include <QPainter>
#include <QCheckBox>
#include <QFontComboBox>
#include <QStackedWidget>
#include <QListWidget>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QGroupBox>
#include <QMenu>
#include <QMenuBar>
#include <QFont>
#include <QSettings>
#include <QtTest>
#include <cmath>

using namespace napkin;

namespace {

qreal relativeLuminance(const QColor& c)
{
    auto channel = [](int v) {
        const qreal s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

// Alpha is how every text token in this codebase expresses emphasis, so a
// contrast check that ignores it is checking a colour nothing ever paints.
QColor composite(const QColor& fg, const QColor& bg)
{
    const qreal a = fg.alphaF();
    return QColor::fromRgbF(fg.redF() * a + bg.redF() * (1 - a),
                            fg.greenF() * a + bg.greenF() * (1 - a),
                            fg.blueF() * a + bg.blueF() * (1 - a));
}

qreal contrast(const QColor& fg, const QColor& bg)
{
    const qreal a = relativeLuminance(composite(fg, bg));
    const qreal b = relativeLuminance(bg);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// A platform theme that disagrees with whatever the user then picks. This is
// the condition the old applyTheme() got wrong: it patched four roles onto the
// platform's palette and left the other sixteen pointing the wrong way.
QPalette darkPlatformPalette()
{
    QPalette p = QApplication::palette();
    p.setColor(QPalette::Window,        QColor( 35,  38,  41));
    p.setColor(QPalette::WindowText,    QColor(252, 252, 252));
    p.setColor(QPalette::Base,          QColor( 27,  30,  32));
    p.setColor(QPalette::Text,          QColor(252, 252, 252));
    p.setColor(QPalette::Button,        QColor( 49,  54,  59));
    p.setColor(QPalette::ButtonText,    QColor(252, 252, 252));
    p.setColor(QPalette::ToolTipBase,   QColor( 49,  54,  59));
    p.setColor(QPalette::ToolTipText,   QColor(252, 252, 252));
    p.setColor(QPalette::AlternateBase, QColor( 35,  38,  41));
    return p;
}

}  // namespace

// A theme is every role or it is none of them.
//
// Running a dark desktop and choosing Light gave white text on a light window
// across the menu bar, the header buttons and the start page's shortcut rows.
// Two independent faults produced it, and each of these tests fails on one of
// them alone.
class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("napkin-test-theme"));
        QCoreApplication::setApplicationName(QStringLiteral("napkin-test-theme"));
        QSettings().clear();

        // Captured before anything else calls applyTheme(), so the process
        // believes it is running under a dark platform theme.
        QApplication::setPalette(darkPlatformPalette());
        QSettings().setValue(QStringLiteral("appearance/theme"),
                             int(SettingsDialog::Theme::System));
        SettingsDialog::applyAppearance();
    }

    void cleanupTestCase() { QSettings().clear(); }

    void everyPairedRoleIsReadableInBothThemes()
    {
        const QList<QPair<QPalette::ColorRole, QPalette::ColorRole>> pairs = {
            {QPalette::WindowText, QPalette::Window},
            {QPalette::Text, QPalette::Base},
            {QPalette::Text, QPalette::AlternateBase},
            {QPalette::ButtonText, QPalette::Button},
            {QPalette::ToolTipText, QPalette::ToolTipBase},
            {QPalette::PlaceholderText, QPalette::Base},
        };

        for (auto theme : {SettingsDialog::Theme::Light, SettingsDialog::Theme::Dark}) {
            QSettings().setValue(QStringLiteral("appearance/theme"), int(theme));
            SettingsDialog::applyAppearance();
            const QPalette p = QApplication::palette();

            for (const auto& [fg, bg] : pairs) {
                const qreal ratio = contrast(p.color(fg), p.color(bg));
                QVERIFY2(ratio >= 4.5,
                         qPrintable(QStringLiteral("theme %1: role %2 on %3 is %4:1")
                                        .arg(int(theme)).arg(int(fg)).arg(int(bg))
                                        .arg(ratio, 0, 'f', 2)));
            }

            // Selected text has to survive on the accent it is drawn over, and
            // the accent is the one colour still inherited from the platform.
            QVERIFY(contrast(p.color(QPalette::HighlightedText),
                             p.color(QPalette::Highlight)) >= 3.0);
        }
    }

    void theChosenTypefaceAndSizeReachTheApplication()
    {
        const QFont before = QApplication::font();

        QSettings().setValue(QStringLiteral("appearance/textScalePercent"), 150);
        SettingsDialog::applyAppearance();
        const qreal scaled = QApplication::font().pointSizeF();
        QVERIFY2(qAbs(scaled - before.pointSizeF() * 1.5) < 0.51,
                 qPrintable(QStringLiteral("%1 -> %2").arg(before.pointSizeF()).arg(scaled)));

        // Applying twice must not compound. Scaling the already-scaled font is
        // the obvious way to write this and it grows without bound every time
        // the settings dialog is saved.
        SettingsDialog::applyAppearance();
        QCOMPARE(QApplication::font().pointSizeF(), scaled);

        QSettings().setValue(QStringLiteral("appearance/textScalePercent"), 100);
        SettingsDialog::applyAppearance();
        QCOMPARE(QApplication::font().pointSizeF(), before.pointSizeF());
    }

    void aCorruptTextScaleCannotMakeNapkinUnreadable()
    {
        QSettings().setValue(QStringLiteral("appearance/textScalePercent"), 0);
        SettingsDialog::applyAppearance();
        QVERIFY(QApplication::font().pointSizeF() >= tokens::kMinPointSize);

        QSettings().setValue(QStringLiteral("appearance/textScalePercent"), 100000);
        QVERIFY(SettingsDialog::textScalePercent() <= 300);

        QSettings().setValue(QStringLiteral("appearance/textScalePercent"), 100);
        SettingsDialog::applyAppearance();
    }

    // Since the 2026-10-06 mockup a list row is part of the sidebar: at rest it
    // has no fill and no edge of its own, and the chosen row is cut out of the
    // panel in the window's colour, as the header's Home / Trash switch is. An
    // edge drawn at rest would put a box around every napkin in the list.
    void aListRowIsPartOfTheSidebarUntilChosen()
    {
        GuiFixture f;
        f.seed("first");
        f.seed("second");
        auto* delegate = f.view()->findChild<BufferCardDelegate*>();
        QVERIFY(delegate);
        const QModelIndex index = f.model()->index(1, 0);   // not under a section label
        QVERIFY(!index.data(BufferListModel::SectionFirstRole).toBool());

        for (auto theme : {SettingsDialog::Theme::Light, SettingsDialog::Theme::Dark}) {
            QSettings().setValue(QStringLiteral("appearance/theme"), int(theme));
            SettingsDialog::applyAppearance();
            const QPalette pal = QApplication::palette();
            const QString name = theme == SettingsDialog::Theme::Light ? "Light" : "Dark";

            for (bool selected : {false, true}) {
                QStyleOptionViewItem option;
                option.initFrom(f.view());
                option.palette = pal;
                option.state = QStyle::State_Enabled;
                if (selected) option.state |= QStyle::State_Selected;
                option.rect = QRect(QPoint(0, 0), QSize(340, delegate->sizeHint(option, index).height()));

                QImage image(option.rect.size(), QImage::Format_RGB32);
                image.fill(pal.color(QPalette::Base));   // the sidebar
                {
                    QPainter p(&image);
                    delegate->paint(&p, option, index);
                }
                // On the row's left edge, and just inside it past the rail.
                const int y = option.rect.height() / 2;
                const QColor edge = image.pixelColor(tokens::kRowMarginX, y);
                // Just inside the row's edge — where the accent rail used to be,
                // so its removal is checked too.
                const QColor inside = image.pixelColor(tokens::kRowMarginX + 3, y);
                const QColor expected = pal.color(selected ? QPalette::Window : QPalette::Base);
                QVERIFY2(inside == expected,
                         qPrintable(QStringLiteral("%1, %2: row is %3, expected %4")
                                        .arg(name, selected ? "chosen" : "at rest",
                                             inside.name(), expected.name())));
                if (!selected)
                    QVERIFY2(edge == pal.color(QPalette::Base),
                             qPrintable(QStringLiteral("%1: a resting row drew an edge %2")
                                            .arg(name, edge.name())));
            }
        }
        QSettings().setValue(QStringLiteral("appearance/theme"), int(SettingsDialog::Theme::System));
        SettingsDialog::applyAppearance();
    }

    // NapkinStyle sized the thumb with clamp(len, 36, barLength): a bar shorter
    // than 36px made the low bound exceed the high one — undefined, and an
    // abort under the Arch package's _GLIBCXX_ASSERTIONS. Without assertions
    // the thumb came out longer than the bar, starting above it.
    void aShortScrollBarStillHasAThumbInsideIt()
    {
        GuiFixture f;   // installs NapkinStyle through applyAppearance
        QVERIFY(QApplication::style()->inherits("napkin::NapkinStyle"));
        QScrollBar bar(Qt::Vertical);
        bar.setRange(0, 100);
        bar.setPageStep(10);
        bar.resize(10, 20);
        QStyleOptionSlider opt;
        opt.initFrom(&bar);
        opt.orientation = Qt::Vertical;
        opt.minimum = 0; opt.maximum = 100; opt.pageStep = 10;
        for (int value : {0, 50, 100}) {
            opt.sliderPosition = opt.sliderValue = value;
            const QRect thumb = QApplication::style()->subControlRect(
                QStyle::CC_ScrollBar, &opt, QStyle::SC_ScrollBarSlider, &bar);
            QVERIFY2(thumb.top() >= 0 && thumb.bottom() < 20 && thumb.height() > 0,
                     qPrintable(QStringLiteral("value %1: thumb %2..%3 in a 20px bar")
                                    .arg(value).arg(thumb.top()).arg(thumb.bottom())));
        }
    }

    // The menu's Theme items change the theme at once and say which is chosen,
    // including a choice made in Settings since the menu was last open.
    void theMenuSwitchesTheThemeAndShowsTheChoice()
    {
        GuiFixture f;
        auto* themeMenu = f.window.findChild<QMenu*>(QStringLiteral("themeMenu"));
        QVERIFY(themeMenu);
        auto item = [&](SettingsDialog::Theme t) -> QAction* {
            for (QAction* a : themeMenu->actions())
                if (a->data().toInt() == int(t)) return a;
            return nullptr;
        };
        QVERIFY(item(SettingsDialog::Theme::System) && item(SettingsDialog::Theme::Light)
                && item(SettingsDialog::Theme::Dark));

        item(SettingsDialog::Theme::Dark)->trigger();
        QCOMPARE(SettingsDialog::theme(), SettingsDialog::Theme::Dark);
        QCOMPARE(QApplication::palette().color(QPalette::Window).rgb(), tokens::kDarkColors.window);

        item(SettingsDialog::Theme::Light)->trigger();
        QCOMPARE(QApplication::palette().color(QPalette::Window).rgb(), tokens::kLightColors.window);

        SettingsDialog::setTheme(SettingsDialog::Theme::Dark);   // as Settings would
        emit themeMenu->aboutToShow();
        QVERIFY(item(SettingsDialog::Theme::Dark)->isChecked());
        QVERIFY(!item(SettingsDialog::Theme::Light)->isChecked());

        SettingsDialog::setTheme(SettingsDialog::Theme::System);
    }

    // Two panes, like KDE's System Settings: a list of pages and the page.
    void theSettingsDialogIsAListOfPagesAndThePage()
    {
        SettingsDialog d;
        d.show();
        QCoreApplication::processEvents();
        auto* pages = d.findChild<QListWidget*>(QStringLiteral("settingsPages"));
        auto* stack = d.findChild<QStackedWidget*>(QStringLiteral("settingsStack"));
        QVERIFY(pages && stack);
        QCOMPARE(pages->count(), stack->count());
        QCOMPARE(pages->count(), 3);
        QCOMPARE(stack->currentIndex(), 0);
        // Every control lives on exactly one page, and choosing that page shows it.
        for (int i = 0; i < pages->count(); ++i) {
            pages->setCurrentRow(i);
            QCOMPARE(stack->currentIndex(), i);
            QVERIFY2(!stack->currentWidget()->findChildren<QWidget*>().isEmpty(),
                     qPrintable(pages->item(i)->text()));
        }
        pages->setCurrentRow(0);
        QVERIFY(stack->currentWidget()->findChild<QFontComboBox*>());   // Appearance holds the typeface
        pages->setCurrentRow(2);
        QVERIFY(stack->currentWidget()->findChild<QCheckBox*>());       // Background holds the tray
    }

    void aChosenAccentSurvivesEveryTheme()
    {
        QSettings().setValue(QStringLiteral("appearance/accent"), QStringLiteral("#27ae60"));

        for (auto theme : {SettingsDialog::Theme::System, SettingsDialog::Theme::Light,
                           SettingsDialog::Theme::Dark}) {
            QSettings().setValue(QStringLiteral("appearance/theme"), int(theme));
            SettingsDialog::applyAppearance();
            const QPalette p = QApplication::palette();

            QCOMPARE(p.color(QPalette::Highlight), QColor(QStringLiteral("#27ae60")));
            // Whatever the user picks, what is written on top of it still has to
            // be readable — the accent is theirs, the pairing is ours.
            QVERIFY2(contrast(p.color(QPalette::HighlightedText),
                              p.color(QPalette::Highlight)) >= 3.0,
                     qPrintable(QStringLiteral("theme %1").arg(int(theme))));
        }

        QSettings().remove(QStringLiteral("appearance/accent"));
        SettingsDialog::applyAppearance();
    }

    void theStartPageIsReadableWhenTheThemeOpposesThePlatform()
    {
        QSettings().setValue(QStringLiteral("appearance/theme"),
                             int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();

        GuiFixture f;   // no buffers, so the start page is what is showing
        const QColor window = QApplication::palette().color(QPalette::Window);

        const auto labels = f.window.findChildren<QLabel*>();
        QVERIFY(labels.size() >= 10);   // the mark, the name, the tagline, the rows

        for (QLabel* label : labels) {
            if (label->text().isEmpty()) continue;   // the logo carries a pixmap
            // The colour the label actually paints with. Reading WindowText
            // here instead is precisely the bug: the shortcut rows live inside
            // buttons and draw with ButtonText, so tinting WindowText on them
            // changed nothing at all.
            const QColor drawn = label->palette().color(label->foregroundRole());
            const qreal ratio = contrast(drawn, window);
            QVERIFY2(ratio >= 3.0,
                     qPrintable(QStringLiteral("“%1” is %2:1 against the window")
                                    .arg(label->text().left(30)).arg(ratio, 0, 'f', 2)));
        }
    }

    void switchingThemeRecoloursTextAlreadyOnScreen()
    {
        // A theme change has to reach cards that are already built. It did not:
        // the editor carried a stylesheet, which makes Qt resolve a palette onto
        // the widget once and then stop following the application's — so card
        // text stayed the old theme's colour, black on a dark card, until
        // something rebuilt the card. Clicking another buffer did that, which is
        // why it looked like a refresh problem rather than a colour one.
        QSettings().setValue(QStringLiteral("appearance/theme"),
                             int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();

        GuiFixture f;
        const auto id = f.seed("words the user actually wrote");
        f.select(id);
        auto* editor = f.canvas()->findChildren<TextItemCard*>().value(0)
                           ->findChild<QPlainTextEdit*>();
        QVERIFY(editor);

        QSettings().setValue(QStringLiteral("appearance/theme"),
                             int(SettingsDialog::Theme::Dark));
        SettingsDialog::applyAppearance();
        QCoreApplication::processEvents();

        // Against the card it is drawn on, not merely "different from before":
        // the failure mode is unreadability.
        const QColor drawn = editor->palette().color(QPalette::Text);
        const QColor card = QApplication::palette().color(QPalette::Base);
        QVERIFY2(contrast(drawn, card) >= 4.5,
                 qPrintable(QStringLiteral("text %1 on card %2 is %3:1")
                                .arg(drawn.name(), card.name())
                                .arg(contrast(drawn, card), 0, 'f', 2)));

        QSettings().setValue(QStringLiteral("appearance/theme"),
                             int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();
    }

    void theShortcutRowsKeepTheirEmphasisInsideAButton()
    {
        // The rows are clickable, so their labels live inside a QPushButton and
        // inherit ButtonText as their foreground role. Tinting WindowText on
        // them — which is what applyPalette() used to do — is a no-op: both
        // labels then paint in the same colour and the key stops standing out
        // from its description. Readability alone will not catch this, because
        // ButtonText is now a perfectly readable colour.
        QSettings().setValue(QStringLiteral("appearance/theme"),
                             int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();

        GuiFixture f;
        auto* key = f.window.findChild<QLabel*>(QStringLiteral("shortcutKey"));
        auto* what = f.window.findChild<QLabel*>(QStringLiteral("shortcutWhat"));
        QVERIFY(key && what);

        const QPalette app = QApplication::palette();
        QCOMPARE(key->palette().color(key->foregroundRole()),
                 tokens::text(app, tokens::kTextPrimary));
        QCOMPARE(what->palette().color(what->foregroundRole()),
                 tokens::text(app, tokens::kTextSecondary));
    }

    // The menu replaced the menu bar, and it is drawn by NapkinStyle from the
    // palette — so it follows Napkin's theme, not the desktop's, with no
    // stylesheet to keep in step. (The menu bar needed one: Breeze painted it in
    // the desktop's header colours.)
    void theMenuFollowsNapkinsThemeNotTheDesktops()
    {
        GuiFixture f;
        auto* button = f.window.findChild<QToolButton*>(QStringLiteral("overflowButton"));
        QVERIFY(button && button->menu());
        QMenu* menu = button->menu();
        for (auto theme : {SettingsDialog::Theme::Dark, SettingsDialog::Theme::Light,
                           SettingsDialog::Theme::Dark}) {
            QSettings().setValue(QStringLiteral("appearance/theme"), int(theme));
            SettingsDialog::applyAppearance();
            QCoreApplication::processEvents();
            menu->popup(QPoint(0, 0));
            QCoreApplication::processEvents();
            const QImage img = menu->grab().toImage();
            menu->hide();
            // Inside the panel, clear of the rounded corners and the items' text.
            const QColor panel = img.pixelColor(img.width() - 4, img.height() / 2);
            const QColor base = QApplication::palette().color(QPalette::Base);
            QVERIFY2(panel == base, qPrintable(QStringLiteral("menu %1, palette Base %2")
                                                   .arg(panel.name(), base.name())));
        }
        QSettings().setValue(QStringLiteral("appearance/theme"), int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();
    }


    void theSettingsDialogFitsItsContentAndPreviewsTheTheme()
    {
        // Usability test: the dialog opened squeezed, clipping the preview and
        // the Lifecycle notes, and the preview ignored the chosen theme.
        SettingsDialog d;
        d.show();
        QCoreApplication::processEvents();
        for (auto* box : d.findChildren<QGroupBox*>())
            QVERIFY2(box->height() >= box->minimumSizeHint().height(), qPrintable(box->title()));

        QLabel* preview = nullptr;
        for (auto* l : d.findChildren<QLabel*>())
            if (l->text().startsWith(QStringLiteral("The quick"))) preview = l;
        QVERIFY(preview);
        auto* theme = d.findChildren<QComboBox*>().first();   // Theme is the first row
        theme->setCurrentIndex(int(SettingsDialog::Theme::Light));
        const QColor light = preview->palette().color(preview->backgroundRole());
        theme->setCurrentIndex(int(SettingsDialog::Theme::Dark));
        const QColor dark = preview->palette().color(preview->backgroundRole());
        QVERIFY2(dark.lightnessF() < light.lightnessF(), "the preview does not show the theme");
    }


    void theToolbarIconsFollowTheTheme()
    {
        GuiFixture f;
        // The gear: the Trash toggle that used to be checked here is a word in
        // the Home / Trash switch now, with no glyph.
        auto* trash = f.window.findChild<QToolButton*>(QStringLiteral("settingsButton"));
        auto* more = f.window.findChild<QToolButton*>(QStringLiteral("overflowButton"));
        QVERIFY(trash && more);
        QVERIFY(!trash->icon().isNull());
        QVERIFY(!more->icon().isNull());
        QVERIFY(more->text().isEmpty() || more->toolButtonStyle() == Qt::ToolButtonIconOnly);

        auto centre = [](const QIcon& icon) {   // the colour the glyph is drawn in
            const QImage img = icon.pixmap(16, 16).toImage();
            QColor best; int alpha = -1;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (img.pixelColor(x, y).alpha() > alpha) { alpha = img.pixelColor(x, y).alpha(); best = img.pixelColor(x, y); }
            return best;
        };
        QSettings().setValue(QStringLiteral("appearance/theme"), int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();
        QCoreApplication::processEvents();
        const QColor light = centre(trash->icon());
        QSettings().setValue(QStringLiteral("appearance/theme"), int(SettingsDialog::Theme::Dark));
        SettingsDialog::applyAppearance();
        QCoreApplication::processEvents();
        const QColor dark = centre(trash->icon());
        QVERIFY2(dark.lightnessF() > light.lightnessF(), "the gear glyph did not follow the theme");
        QSettings().setValue(QStringLiteral("appearance/theme"), int(SettingsDialog::Theme::Light));
        SettingsDialog::applyAppearance();
    }


    void theToolbarUsesTheBundledLucideIcons()
    {
        if (!QImageReader::supportedImageFormats().contains("svg"))
            QSKIP("no SVG image plugin here; the drawn fallbacks are used instead");
        for (const char* name : {"plus", "menu", "search", "trash-2", "settings"}) {
            QImageReader reader(QStringLiteral(":/resources/icons/lucide/%1.svg").arg(QLatin1String(name)));
            QVERIFY2(!reader.read().isNull(), name);
        }
        GuiFixture f;
        for (const char* button : {"newButton", "overflowButton", "settingsButton"}) {
            auto* b = f.window.findChild<QAbstractButton*>(QString::fromLatin1(button));
            QVERIFY2(b && !b->icon().isNull(), button);
        }
    }

    void theSettingsButtonOpensSettings()
    {
        GuiFixture f;
        auto* gear = f.window.findChild<QToolButton*>(QStringLiteral("settingsButton"));
        QVERIFY(gear);
        bool opened = false;
        QTimer::singleShot(50, [&] {
            for (QWidget* w : QApplication::topLevelWidgets())
                if (auto* d = qobject_cast<SettingsDialog*>(w)) { opened = d->isVisible(); d->reject(); }
        });
        gear->click();
        QVERIFY(opened);
    }

};

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
