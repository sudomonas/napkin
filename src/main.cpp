#include "app/Application.h"
#include "app/Paths.h"
#include "app/SingleInstance.h"
#include "data/BufferRepository.h"
#include "data/Database.h"
#include "data/ItemRepository.h"
#include "domain/BufferService.h"
#include "media/BlobStore.h"
#include "media/Thumbnailer.h"
#include "ui/MainWindow.h"
#include "ui/TourDialog.h"

#include <QTimer>
#include "ui/SettingsDialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>

using namespace napkin;

int main(int argc, char** argv)
{
    Application app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("napkin"));
    QCoreApplication::setOrganizationName(QStringLiteral("napkin"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.2"));
    // Must match the .desktop file's basename, or Wayland gives the window no
    // icon and no taskbar identity. Until now this named a file that did not
    // exist anywhere.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.sudomonas.Napkin"));

    // Answered before anything else happens — before the single-instance
    // check, which would otherwise hand "--version" to a running Napkin and
    // print nothing, and before the database is touched. A packaged build has
    // to be startable and identifiable from a terminal without side effects.
    {
        QCommandLineParser parser;
        parser.setApplicationDescription(
            QStringLiteral("Napkin — a persistent scratch surface for your computer.\n"
                           "Paste text and images into it. Everything stays on this machine."));
        const QCommandLineOption help = parser.addHelpOption();
        const QCommandLineOption version = parser.addVersionOption();
        parser.parse(QCoreApplication::arguments());
        if (parser.isSet(version)) { parser.showVersion(); return 0; }
        if (parser.isSet(help))    { parser.showHelp(0); }
    }

    // Prefer the installed theme icon; fall back to the copies compiled in, so
    // a build straight out of the source tree still has an icon.
    QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.sudomonas.Napkin"));
    if (icon.isNull()) {
        for (const char* size : {"16", "32", "48", "64", "128", "256"})
            icon.addFile(QStringLiteral(":/resources/icons/%1x%1/io.github.sudomonas.Napkin.png").arg(size));
    }
    QApplication::setWindowIcon(icon);

    auto cannotStart = [](const std::exception& e) {
        // SPEC.md §14: plain language, no stack traces, content accounted for.
        QMessageBox::critical(
            nullptr, QStringLiteral("Napkin cannot start"),
            QStringLiteral("Napkin could not open its storage, so it has not started.\n\n"
                           "No data has been changed.\n\n%1")
                .arg(QString::fromUtf8(e.what())));
        return 1;
    };

    try {
        paths::ensureDirs();   // the instance lock lives in here, so first
    } catch (const std::exception& e) {
        return cannotStart(e);
    }

    // Before the database is opened, not after: a second launch used to open
    // the file and run its migrations against a database another Napkin was
    // using, and only then discover it should not be running at all.
    SingleInstance instance;
    switch (instance.acquire()) {
    case SingleInstance::Outcome::Primary:
        break;
    case SingleInstance::Outcome::HandedOff:
        return 0;   // the running Napkin was asked to raise itself
    case SingleInstance::Outcome::NotResponding:
        // Saying so, rather than vanishing: from the outside a launch that
        // quietly exits looks exactly like Napkin being broken.
        QMessageBox::warning(
            nullptr, QStringLiteral("Napkin is already running"),
            QStringLiteral("Napkin is already running, but it did not answer when asked "
                           "to show its window.\n\nSwitch to it, or close it and try "
                           "again. A second copy has not been started, so nothing has "
                           "been changed."));
        return 1;
    }

    Database db;
    BufferRepository buffers(db);
    ItemRepository items(db);

    try {
        db.open(paths::databaseFile());
        paths::secureDatabaseFiles();  // the files exist only now, on a first run
    } catch (const std::exception& e) {
        return cannotStart(e);
    }

    SettingsDialog::applyAppearance();   // before any window exists, so nothing flashes
    SettingsDialog::followSystemChanges();

    BufferService service(db, buffers, items);
    service.purgeExpiredTrash();  // the only automatic hard delete (§6)

    BlobStore blobs(paths::blobsDir());
    Thumbnailer thumbs(paths::thumbsDir(), blobs);
    MainWindow window(db, buffers, items, service, blobs, thumbs);
    // Orphans and stale thumbnails, collected in the background: walking the
    // whole store before the window appeared made startup grow with it.
    window.sweepBlobs();
    QObject::connect(&instance, &SingleInstance::raiseRequested,
                     &window, &MainWindow::raiseFromOtherInstance);
    window.show();
    // Once, on the first launch, after the window is on screen so the tour has
    // something behind it. Here and not in MainWindow, so a window built by a
    // test never meets a modal dialog.
    if (!TourDialog::seen())
        QTimer::singleShot(400, &window, &MainWindow::showTour);
    return app.exec();
}
