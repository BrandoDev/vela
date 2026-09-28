// vela-shell: taskbar e menu Start di Vela.
//
// Ogni pezzo della shell è una finestra "layer-shell": il compositor sa che
// è parte del desktop (non un'app), la tiene sopra o sotto le finestre e le
// riserva spazio sullo schermo.

#include "appmodel.h"
#include "foreigntoplevels.h"
#include "iconprovider.h"
#include "notifications.h"
#include "shellcontroller.h"
#include "taskbarmodel.h"
#include "tray.h"
#include "wallpaperprovider.h"
#include "windowcapture.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QtDebug>

#include <memory>

namespace {

using LayerWindow = LayerShellQt::Window;

QQuickWindow* findWindow(QQmlApplicationEngine& engine, const char* objectName)
{
    for (QObject* root : engine.rootObjects()) {
        if (root->objectName() == QLatin1String(objectName)) {
            return qobject_cast<QQuickWindow*>(root);
        }
    }
    return nullptr;
}

void setupTaskbar(QQuickWindow* window, int height)
{
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-taskbar"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft
        | LayerWindow::AnchorRight);
    layer->setExclusiveZone(height); // le finestre massimizzate non la coprono
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupWallpaper(QQuickWindow* window)
{
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-wallpaper"));
    layer->setLayer(LayerWindow::LayerBackground);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1); // tutto lo schermo, anche sotto la taskbar
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupSwitcher(QQuickWindow* window)
{
    // A tutto schermo e trasparente: il pannello sta al centro. Non prende
    // la tastiera, che resta al compositor (è lui a gestire Alt+Tab).
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-switcher"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupStartMenu(QQuickWindow* window)
{
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-start-menu"));
    layer->setLayer(LayerWindow::LayerTop);
    // Ancorato solo in basso: il compositor lo centra orizzontalmente e lo
    // mette sopra la taskbar (che ha riservato il suo spazio). Niente
    // margine: la finestra tocca la taskbar, così il pannello che sale viene
    // tagliato lì e sembra uscire da dietro la taskbar, come su Windows 11.
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom));
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupNotifications(QQuickWindow* window)
{
    // In basso a destra, sopra la taskbar (che ha riservato il suo spazio)
    // e sopra le finestre. Non prende la tastiera.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-notifications"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorRight);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupTrayMenu(QQuickWindow* window)
{
    // Tutto lo schermo, trasparente: il menu sta sopra l'icona e un clic
    // fuori lo chiude. Prende la tastiera (Esc) e la perde cliccando altrove.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-tray-menu"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

// Sfondo e taskbar ci devono essere sempre. Il compositor chiude le
// superfici della shell quando il loro schermo sparisce: un monitor
// scollegato, o il cambio di console (wlroots toglie tutti gli schermi e li
// ricrea al ritorno). Qui le si rimette sullo schermo principale appena ce
// n'è uno vero.
void keepShown(QGuiApplication& app, const QList<QQuickWindow*>& windows)
{
    auto* timer = new QTimer(&app);
    timer->setSingleShot(true);
    timer->setInterval(100); // gli schermi tornano uno alla volta: si aspetta che arrivino
    auto retries = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, &app, [windows] {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen || screen->name().isEmpty()) {
            return; // solo il segnaposto di Qt: nessuno schermo vero, si aspetta screenAdded
        }
        for (QQuickWindow* window : windows) {
            if (!window->isVisible()) {
                qInfo("vela-shell: %s di nuovo su %s", qPrintable(window->objectName()), qPrintable(screen->name()));
                window->setScreen(screen);
                window->show();
            }
        }
    });
    for (QQuickWindow* window : windows) {
        QObject::connect(window, &QWindow::visibleChanged, timer, [timer, retries](bool visible) {
            // Pochi tentativi senza uno schermo nuovo: se il compositor la
            // richiude subito, non si insiste all'infinito.
            if (!visible && *retries < 5) {
                ++*retries;
                timer->start();
            }
        });
    }
    QObject::connect(&app, &QGuiApplication::screenAdded, timer, [timer, retries] {
        *retries = 0;
        timer->start();
    });
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("vela-shell"));
    QGuiApplication::setOrganizationName(QStringLiteral("Vela"));
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // "vela-shell --toggle-start": comodo per scorciatoie e script.
    const QStringList args = QGuiApplication::arguments();
    if (args.contains(QStringLiteral("--toggle-start"))) {
        return ShellController::sendToRunningInstance("toggle-start") ? 0 : 1;
    }
    if (ShellController::sendToRunningInstance("ping")) {
        qWarning("vela-shell: già in esecuzione in questa sessione");
        return 0;
    }

    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        qWarning("vela-shell: serve una sessione Wayland (piattaforma attuale: %s)",
            qPrintable(QGuiApplication::platformName()));
        return 1;
    }

    // Fuori da Plasma Qt potrebbe non conoscere il tema di icone scelto.
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        QIcon::setThemeName(qEnvironmentVariable("VELA_ICON_THEME", QStringLiteral("breeze-dark")));
    }
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

    AppModel apps;
    apps.reload();

    ShellController shell;
    shell.listen();
    shell.watchSleep();

    ForeignToplevelManager windows;
    TaskbarModel tasks(&apps, &windows);
    WindowCapture capture;
    NotificationServer notifications;
    notifications.registerService();
    TrayModel tray;
    tray.start();

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("wallpaper"), new WallpaperProvider);
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider(&capture));
    engine.addImageProvider(QStringLiteral("notification"), new NotificationImageProvider(&notifications));
    engine.addImageProvider(QStringLiteral("tray"), new TrayImageProvider(&tray));
    engine.rootContext()->setContextProperty(QStringLiteral("Apps"), &apps);
    engine.rootContext()->setContextProperty(QStringLiteral("Shell"), &shell);
    engine.rootContext()->setContextProperty(QStringLiteral("Tasks"), &tasks);
    engine.rootContext()->setContextProperty(QStringLiteral("Capture"), &capture);
    engine.rootContext()->setContextProperty(QStringLiteral("Notifications"), &notifications);
    engine.rootContext()->setContextProperty(QStringLiteral("Tray"), &tray);

    // Le finestre QML partono invisibili: le trasformiamo in superfici
    // layer-shell PRIMA che vengano mostrate.
    engine.loadFromModule("Vela.Shell", "Wallpaper");
    engine.loadFromModule("Vela.Shell", "Taskbar");
    engine.loadFromModule("Vela.Shell", "StartMenu");
    engine.loadFromModule("Vela.Shell", "Switcher");
    engine.loadFromModule("Vela.Shell", "NotificationPopups");
    engine.loadFromModule("Vela.Shell", "TrayMenu");

    QQuickWindow* switcher = findWindow(engine, "switcher");
    QQuickWindow* wallpaper = findWindow(engine, "wallpaper");
    QQuickWindow* taskbar = findWindow(engine, "taskbar");
    QQuickWindow* startMenu = findWindow(engine, "startMenu");
    QQuickWindow* notificationWindow = findWindow(engine, "notifications");
    QQuickWindow* trayMenu = findWindow(engine, "trayMenu");
    if (!wallpaper || !taskbar || !startMenu || !switcher || !notificationWindow || !trayMenu) {
        qCritical("vela-shell: impossibile caricare l'interfaccia QML");
        return 1;
    }

    setupWallpaper(wallpaper);
    setupTaskbar(taskbar, taskbar->height());
    setupStartMenu(startMenu);
    setupSwitcher(switcher);
    setupNotifications(notificationWindow);
    setupTrayMenu(trayMenu);
    wallpaper->show();
    taskbar->show();
    keepShown(app, { wallpaper, taskbar });

    return app.exec();
}
