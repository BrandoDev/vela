// vela-shell: taskbar e menu Start di Vela.
//
// Ogni pezzo della shell è una finestra "layer-shell": il compositor sa che
// è parte del desktop (non un'app), la tiene sopra o sotto le finestre e le
// riserva spazio sullo schermo.

#include "appmodel.h"
#include "desktopmodel.h"
#include "foreigntoplevels.h"
#include "iconprovider.h"
#include "jumplists.h"
#include "notifications.h"
#include "servicemenus.h"
#include "shellcontroller.h"
#include "systemactions.h"
#include "taskbarmodel.h"
#include "tray.h"
#include "wallpaperprovider.h"
#include "wallpapers.h"
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

void setupContextMenu(QQuickWindow* window)
{
    // I menu del tasto destro: tutto lo schermo, trasparente, sopra a tutto.
    // I menu stanno dentro, e un clic fuori li chiude. Prende la tastiera
    // (frecce, Invio, Esc) e la perde cliccando altrove.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-context-menu"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupRunDialog(QQuickWindow* window)
{
    // "Esegui", in basso a sinistra sopra la taskbar, come in Windows.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-run"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft);
    layer->setMargins(QMargins(12, 0, 0, 12));
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupConfirmDialog(QQuickWindow* window)
{
    // Le domande (es. "Svuota Cestino"): al centro dello schermo, sopra le finestre.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-confirm"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

// La taskbar ci deve essere sempre (gli sfondi li segue Wallpapers). Il
// compositor chiude le superfici della shell quando il loro schermo
// sparisce: un monitor scollegato, o il cambio di console (wlroots toglie
// tutti gli schermi e li ricrea al ritorno). Qui la si rimette sullo
// schermo principale appena ce n'è uno vero.
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
    SystemActions system;
    JumpLists jumps(&apps);
    DesktopModel desktop(&apps);
    ServiceMenus serviceMenus;

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
    engine.rootContext()->setContextProperty(QStringLiteral("System"), &system);
    engine.rootContext()->setContextProperty(QStringLiteral("Jumps"), &jumps);
    engine.rootContext()->setContextProperty(QStringLiteral("Desktop"), &desktop);
    engine.rootContext()->setContextProperty(QStringLiteral("ServiceMenus"), &serviceMenus);

    // Le finestre QML partono invisibili: le trasformiamo in superfici
    // layer-shell PRIMA che vengano mostrate.
    engine.loadFromModule("Vela.Shell", "Taskbar");
    engine.loadFromModule("Vela.Shell", "StartMenu");
    engine.loadFromModule("Vela.Shell", "Switcher");
    engine.loadFromModule("Vela.Shell", "NotificationPopups");
    engine.loadFromModule("Vela.Shell", "ContextMenu");
    engine.loadFromModule("Vela.Shell", "RunDialog");
    engine.loadFromModule("Vela.Shell", "ConfirmDialog");

    QQuickWindow* switcher = findWindow(engine, "switcher");
    QQuickWindow* taskbar = findWindow(engine, "taskbar");
    QQuickWindow* startMenu = findWindow(engine, "startMenu");
    QQuickWindow* notificationWindow = findWindow(engine, "notifications");
    QQuickWindow* contextMenu = findWindow(engine, "contextMenu");
    QQuickWindow* runDialog = findWindow(engine, "runDialog");
    QQuickWindow* confirmDialog = findWindow(engine, "confirmDialog");
    if (!taskbar || !startMenu || !switcher || !notificationWindow || !contextMenu || !runDialog || !confirmDialog) {
        qCritical("vela-shell: impossibile caricare l'interfaccia QML");
        return 1;
    }

    setupTaskbar(taskbar, taskbar->height());
    setupStartMenu(startMenu);
    setupSwitcher(switcher);
    setupNotifications(notificationWindow);
    setupContextMenu(contextMenu);
    setupRunDialog(runDialog);
    setupConfirmDialog(confirmDialog);
    taskbar->show();
    keepShown(app, { taskbar });
    Wallpapers wallpapers(&engine);
    if (!wallpapers.start()) {
        return 1;
    }

    return app.exec();
}
