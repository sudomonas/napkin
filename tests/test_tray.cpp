#include "../src/ui/SettingsDialog.h"
#include "../src/ui/TrayIcon.h"
#include "GuiFixture.h"

#include <QCloseEvent>
#include <QSettings>
#include <QClipboard>
#include <QLabel>
#include <QtTest>

using namespace napkin;

// Keeping Napkin in the tray is the setting that makes "somewhere to throw
// things" work — it can only catch what is thrown at it if it is running. It is
// also the setting that can strand a user, so the tests are mostly about that.
class TestTray : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("napkin-test-tray"));
        QCoreApplication::setApplicationName(QStringLiteral("napkin-test-tray"));
        QSettings().clear();
    }

    void cleanupTestCase() { QSettings().clear(); }

    void offByDefault()
    {
        QSettings().clear();
        QVERIFY(!SettingsDialog::keepInTray());
    }

    void theSettingIsRefusedOnADesktopWithNoTray()
    {
        // The stored value is only half the answer. A session with no
        // StatusNotifier host cannot honour it however it is stored, and
        // answering true there is what would let the window close to a place
        // that does not exist. The offscreen platform has no tray, so this is
        // that case rather than a simulation of it.
        QSettings().setValue(QStringLiteral("behaviour/keepInTray"), true);

        if (TrayIcon::availableOnThisDesktop())
            QSKIP("this session has a tray, so the refusal cannot be observed here");

        QCOMPARE(SettingsDialog::keepInTray(), false);
        QSettings().remove(QStringLiteral("behaviour/keepInTray"));
    }

    void closingStillClosesWhenThereIsNoTrayToHideTo()
    {
        // The trap this guards: setting on, tray absent, window closes to
        // nothing and the application is unreachable with its data still in it.
        QSettings().setValue(QStringLiteral("behaviour/keepInTray"), true);

        GuiFixture f;
        f.seed("something worth not losing");

        QCloseEvent event;
        QCoreApplication::sendEvent(&f.window, &event);

        if (TrayIcon::availableOnThisDesktop())
            QSKIP("this session has a tray; the strand case cannot arise here");

        QVERIFY2(event.isAccepted(),
                 "the window refused to close with no tray to hide into");
        QSettings().remove(QStringLiteral("behaviour/keepInTray"));
    }


    // --- the global capture shortcut ---------------------------------------------
    // What the shortcut does once the desktop reports it. The portal half needs
    // a real desktop and a person to press the key; this half does not.
    void theCaptureShortcutIsOffUntilAskedFor()
    {
        QVERIFY(!napkin::SettingsDialog::captureShortcut());
    }

    void theShortcutPastesOntoTheOpenNapkinAndSaysWhich()
    {
        GuiFixture f;
        f.window.activateWindow();
        const auto id = f.seed("Research notes");
        f.model()->reload();
        f.select(id);
        QApplication::clipboard()->setText(QStringLiteral("copied from the browser"));

        f.window.pasteFromGlobalShortcut();
        QTRY_COMPARE_WITH_TIMEOUT(f.items.countForBuffer(id), 2, 3000);
        bool landed = false;
        for (const auto& item : f.items.listForBuffer(id))
            if (item.text == QStringLiteral("copied from the browser")) landed = true;
        QVERIFY(landed);
        // Said in the capture window, which then closes by itself; the main
        // window is never raised (on Wayland it cannot be, from the background).
        auto* capture = qobject_cast<QLabel*>(f.window.captureWindowForTest());
        QVERIFY(capture);
        QTRY_VERIFY_WITH_TIMEOUT(capture->text().contains(QStringLiteral("Research notes")), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.captureWindowForTest(), 5000);
    }

    void anEmptyClipboardSaysSoRatherThanClaimingAPaste()
    {
        GuiFixture f;
        f.window.activateWindow();
        const auto id = f.seed("Research notes");
        f.model()->reload();
        f.select(id);
        QApplication::clipboard()->clear();

        f.window.pasteFromGlobalShortcut();
        auto* capture = qobject_cast<QLabel*>(f.window.captureWindowForTest());
        QVERIFY(capture);
        QTRY_VERIFY_WITH_TIMEOUT(capture->text().contains(QStringLiteral("Nothing on the clipboard")),
                                 3000);
        QCOMPARE(f.items.countForBuffer(id), 1);
    }
};

QTEST_MAIN(TestTray)
#include "test_tray.moc"
