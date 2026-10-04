// vela-files: Esplora file di Vela, come quello di Windows 11.
//
//   vela-files [cartella o file...]   (un file: si apre la sua cartella, con il file selezionato)
//
// Obiettivo: avvio a freddo sotto i 150 ms (README, "Obiettivi
// misurabili"). Per questo niente Qt Quick Controls, e ciò che non serve al
// primo fotogramma (le app per "Apri con", i service menu) si prepara dopo.
// Con VELA_FILES_TIMING=1 stampa quanto ci ha messo.

#include "appearance.h"
#include "appmodel.h"
#include "fileops.h"
#include "filethumbnails.h"
#include "foldermodel.h"
#include "iconprovider.h"
#include "places.h"
#include "servicemenus.h"
#include "systemactions.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <ctime>
#include <unistd.h>

namespace {

// Da quanto è partito il processo (ms), da /proc/self/stat.
double sinceProcessStart()
{
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (!stat.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray line = stat.readAll();
    const QList<QByteArray> fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
    if (fields.size() < 20) {
        return -1;
    }
    const double startTicks = fields.at(19).toDouble(); // campo 22 di stat
    timespec now {};
    clock_gettime(CLOCK_BOOTTIME, &now);
    const double ticks = double(sysconf(_SC_CLK_TCK));
    return (now.tv_sec + now.tv_nsec / 1e9 - startTicks / ticks) * 1000.0;
}

} // namespace

int main(int argc, char* argv[])
{
    QElapsedTimer sinceMain;
    sinceMain.start();
    const bool timing = qEnvironmentVariableIntValue("VELA_FILES_TIMING") > 0;

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("vela-files"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Esplora file"));
    QGuiApplication::setOrganizationName(QStringLiteral("Vela"));
    QGuiApplication::setDesktopFileName(QStringLiteral("vela-files"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("system-file-manager")));

    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    // La modalità delle app, l'accento e Mica dalla shell; le icone del
    // tema adatto (breeze o breeze-dark).
    Appearance appearance;

    // Dove aprirsi: la cartella data, o la cartella del file dato (selezionato).
    QString start = QStringLiteral("home:");
    QString select;
    const QStringList args = app.arguments().mid(1);
    if (!args.isEmpty()) {
        const QUrl url = QUrl::fromUserInput(args.first(), QDir::currentPath(), QUrl::AssumeLocalFile);
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : args.first());
        if (info.isDir()) {
            start = info.absoluteFilePath();
        } else if (info.exists()) {
            start = info.absolutePath();
            select = info.absoluteFilePath();
        }
    }

    qmlRegisterType<FolderModel>("Vela.Files.Backend", 1, 0, "FolderModel");

    AppModel apps;
    FileOps ops(&apps);
    Places places;
    SystemActions system;
    ServiceMenus serviceMenus;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("fileicon"), new IconProvider(32));
    engine.addImageProvider(QStringLiteral("filethumb"), new FileThumbnailProvider);
    QQmlContext* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("Apps"), &apps);
    context->setContextProperty(QStringLiteral("Ops"), &ops);
    context->setContextProperty(QStringLiteral("Places"), &places);
    context->setContextProperty(QStringLiteral("System"), &system);
    context->setContextProperty(QStringLiteral("ServiceMenus"), &serviceMenus);
    context->setContextProperty(QStringLiteral("Look"), &appearance);
    context->setContextProperty(QStringLiteral("StartLocation"), start);
    context->setContextProperty(QStringLiteral("StartSelection"), select);
    engine.loadFromModule("Vela.Files", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&, timing] {
            static bool first = true;
            if (!first) {
                return;
            }
            first = false;
            if (timing) {
                std::fprintf(stderr, "vela-files: primo fotogramma dopo %.0f ms da main, %.0f ms dall'avvio del processo\n",
                    double(sinceMain.nsecsElapsed()) / 1e6, sinceProcessStart());
            }
            // Ciò che non serve al primo fotogramma.
            QTimer::singleShot(0, &app, [&apps, &appearance] {
                apps.reload();
                appearance.watch();
            });
        }, Qt::QueuedConnection);
    }
    return app.exec();
}

