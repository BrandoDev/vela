// vela-shell: taskbar e menu Start di Vela.
//
// Ogni pezzo della shell è una finestra "layer-shell": il compositor sa che
// è parte del desktop (non un'app), la tiene sopra o sotto le finestre e le
// riserva spazio sullo schermo.

#include "appmodel.h"
#include "backgroundeffects.h"
#include "config.h"
#include "desktopmodel.h"
#include "fileproperties.h"
#include "filethumbnails.h"
#include "foreigntoplevels.h"
#include "iconprovider.h"
#include "jumplists.h"
#include "notifications.h"
#include "servicemenus.h"
#include "shellcontroller.h"
#include "systemactions.h"
#include "systemstatus.h"
#include "taskbarmodel.h"
#include "tray.h"
#include "wallpaperprovider.h"
#include "wallpapers.h"
#include "workspaces.h"
#include "windowcapture.h"

#include <LayerShellQt/Window>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QtDebug>

#include <cstdio>
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

// Con la taskbar allineata a sinistra la Start si apre in basso a sinistra.
void placeStartMenu(QQuickWindow* window, const QString& alignment)
{
    LayerWindow* layer = LayerWindow::get(window);
    const bool left = alignment == QLatin1String("left");
    layer->setAnchors(left ? LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft
                           : LayerWindow::Anchors(LayerWindow::AnchorBottom));
    layer->setMargins(QMargins(left ? 12 : 0, 0, 0, 0));
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

void setupSourceChooser(QQuickWindow* window)
{
    // Cosa condividere: al centro dello schermo principale, sopra a tutto.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-share"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupPropertiesDialog(QQuickWindow* window)
{
    // Proprietà: al centro dello schermo, sopra le finestre come una finestra di dialogo.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-properties"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupTaskView(QQuickWindow* window)
{
    // Visualizzazione attività: tutto lo spazio sopra la taskbar (che resta
    // visibile e cliccabile, come in Windows), con la tastiera.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-task-view"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityExclusive);
}

void setupDesktopOsd(QQuickWindow* window)
{
    // Il nome del desktop al centro dello schermo: senza ancore il
    // compositor lo centra. Non prende né tastiera né clic.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-desktop-osd"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
    window->setFlag(Qt::WindowTransparentForInput);
}

void setupSidePanel(QQuickWindow* window, const QString& scope)
{
    // Impostazioni rapide e centro notifiche: in basso a destra, sopra la
    // taskbar (che ha riservato il suo spazio), sopra le finestre.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(scope);
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorRight);
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
    // "vela-shell --choose-source": il selettore che xdg-desktop-portal-wlr
    // lancia quando un'app vuole condividere lo schermo. Chiede alla shell
    // già aperta, che mostra la scelta, e stampa la risposta per il portale.
    if (argc > 1 && QByteArray(argv[1]) == "--choose-source") {
        QCoreApplication app(argc, argv);
        const QByteArray answer = ShellController::askRunningInstance("choose-source");
        if (!answer.isEmpty()) {
            std::printf("%s\n", answer.constData());
        }
        return 0;
    }

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

    Config config;
    ShellController shell;
    Workspaces workspaces;
    QObject::connect(&shell, &ShellController::workspacesReceived, &workspaces, &Workspaces::update);
    workspaces.query();
    shell.listen();
    shell.sendWallpaperTint(config.wallpaper());
    QObject::connect(&config, &Config::wallpaperChanged, &shell, [&] { shell.sendWallpaperTint(config.wallpaper()); });
    shell.watchSleep();

    ForeignToplevelManager windows;
    TaskbarModel tasks(&apps, &windows);
    WindowCapture capture;
    NotificationServer notifications;
    notifications.registerService();
    notifications.setDoNotDisturb(config.doNotDisturb());
    QObject::connect(&config, &Config::doNotDisturbChanged, &notifications, &NotificationServer::setDoNotDisturb);
    TrayModel tray;
    tray.start();
    SystemActions system;
    QObject::connect(&shell, &ShellController::settingsRequested, &system, [&system] { system.trigger(QStringLiteral("settings")); });
    JumpLists jumps(&apps);
    DesktopModel desktop(&apps);
    ServiceMenus serviceMenus;
    FileProperties properties;
    BackgroundEffects effects;
    SystemStatus status;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("wallpaper"), new WallpaperProvider);
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider(&capture));
    engine.addImageProvider(QStringLiteral("filethumb"), new FileThumbnailProvider);
    engine.addImageProvider(QStringLiteral("notification"), new NotificationImageProvider(&notifications));
    engine.addImageProvider(QStringLiteral("tray"), new TrayImageProvider(&tray));
    engine.rootContext()->setContextProperty(QStringLiteral("Apps"), &apps);
    engine.rootContext()->setContextProperty(QStringLiteral("Shell"), &shell);
    engine.rootContext()->setContextProperty(QStringLiteral("Config"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("Desktops"), &workspaces);
    engine.rootContext()->setContextProperty(QStringLiteral("Tasks"), &tasks);
    engine.rootContext()->setContextProperty(QStringLiteral("Capture"), &capture);
    engine.rootContext()->setContextProperty(QStringLiteral("Notifications"), &notifications);
    engine.rootContext()->setContextProperty(QStringLiteral("Tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("System"), &system);
    engine.rootContext()->setContextProperty(QStringLiteral("Jumps"), &jumps);
    engine.rootContext()->setContextProperty(QStringLiteral("Desktop"), &desktop);
    engine.rootContext()->setContextProperty(QStringLiteral("ServiceMenus"), &serviceMenus);
    engine.rootContext()->setContextProperty(QStringLiteral("Properties"), &properties);
    engine.rootContext()->setContextProperty(QStringLiteral("Effects"), &effects);
    engine.rootContext()->setContextProperty(QStringLiteral("Status"), &status);

    // Le finestre QML partono invisibili: le trasformiamo in superfici
    // layer-shell PRIMA che vengano mostrate.
    engine.loadFromModule("Vela.Shell", "Taskbar");
    engine.loadFromModule("Vela.Shell", "StartMenu");
    engine.loadFromModule("Vela.Shell", "Switcher");
    engine.loadFromModule("Vela.Shell", "NotificationPopups");
    engine.loadFromModule("Vela.Shell", "ContextMenu");
    engine.loadFromModule("Vela.Shell", "RunDialog");
    engine.loadFromModule("Vela.Shell", "ConfirmDialog");
    engine.loadFromModule("Vela.Shell", "SourceChooser");
    engine.loadFromModule("Vela.Shell", "PropertiesDialog");
    engine.loadFromModule("Vela.Shell", "QuickSettings");
    engine.loadFromModule("Vela.Shell", "NotificationCenter");
    engine.loadFromModule("Vela.Shell", "TaskView");
    engine.loadFromModule("Vela.Shell", "DesktopOsd");

    QQuickWindow* switcher = findWindow(engine, "switcher");
    QQuickWindow* taskbar = findWindow(engine, "taskbar");
    QQuickWindow* startMenu = findWindow(engine, "startMenu");
    QQuickWindow* notificationWindow = findWindow(engine, "notifications");
    QQuickWindow* contextMenu = findWindow(engine, "contextMenu");
    QQuickWindow* runDialog = findWindow(engine, "runDialog");
    QQuickWindow* confirmDialog = findWindow(engine, "confirmDialog");
    QQuickWindow* sourceChooser = findWindow(engine, "sourceChooser");
    QQuickWindow* propertiesDialog = findWindow(engine, "propertiesDialog");
    QQuickWindow* quickSettings = findWindow(engine, "quickSettings");
    QQuickWindow* notificationCenter = findWindow(engine, "notificationCenter");
    QQuickWindow* taskView = findWindow(engine, "taskView");
    QQuickWindow* desktopOsd = findWindow(engine, "desktopOsd");
    if (!taskbar || !startMenu || !switcher || !notificationWindow || !contextMenu || !runDialog || !confirmDialog || !sourceChooser || !propertiesDialog || !quickSettings
        || !notificationCenter || !taskView || !desktopOsd) {
        qCritical("vela-shell: impossibile caricare l'interfaccia QML");
        return 1;
    }

    setupTaskbar(taskbar, taskbar->height());
    setupStartMenu(startMenu);
    placeStartMenu(startMenu, config.taskbarAlignment());
    QObject::connect(&config, &Config::taskbarChanged, startMenu, [&config, startMenu] {
        placeStartMenu(startMenu, config.taskbarAlignment());
    });
    setupSwitcher(switcher);
    setupNotifications(notificationWindow);
    setupContextMenu(contextMenu);
    setupRunDialog(runDialog);
    setupConfirmDialog(confirmDialog);
    setupSourceChooser(sourceChooser);
    setupPropertiesDialog(propertiesDialog);
    setupSidePanel(quickSettings, QStringLiteral("vela-quick-settings"));
    setupSidePanel(notificationCenter, QStringLiteral("vela-notification-center"));
    setupTaskView(taskView);
    setupDesktopOsd(desktopOsd);
    taskbar->show();
    keepShown(app, { taskbar });
    Wallpapers wallpapers(&engine);
    if (!wallpapers.start()) {
        return 1;
    }

    return app.exec();
}
