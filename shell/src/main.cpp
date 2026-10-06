// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// vela-shell: taskbar e menu Start di Vela.
//
// Ogni pezzo della shell è una finestra "layer-shell": il compositor sa che
// è parte del desktop (non un'app), la tiene sopra o sotto le finestre e le
// riserva spazio sullo schermo.

#include "accessibility.h"
#include "appmodel.h"
#include "backgroundeffects.h"
#include "clipboard.h"
#include "config.h"
#include "desktopmodel.h"
#include "fileactions.h"
#include "fileproperties.h"
#include "filethumbnails.h"
#include "foreigntoplevels.h"
#include "iconprovider.h"
#include "jumplists.h"
#include "network.h"
#include "notifications.h"
#include "servicemenus.h"
#include "shellcontroller.h"
#include "snip.h"
#include "systemactions.h"
#include "systemstatus.h"
#include "taskbarmodel.h"
#include "tray.h"
#include "wallpaperprovider.h"
#include "screenwindows.h"
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

void setupTaskbar(QQuickWindow* window, QScreen* screen)
{
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScreen(screen); // una per schermo (ScreenWindows)
    layer->setScope(QStringLiteral("vela-taskbar"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft
        | LayerWindow::AnchorRight);
    layer->setExclusiveZone(window->height()); // le finestre massimizzate non la coprono
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupWallpaper(QQuickWindow* window, QScreen* screen)
{
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScreen(screen); // proprio questo schermo, non quello che sceglierebbe il compositor
    layer->setScope(QStringLiteral("vela-wallpaper"));
    layer->setLayer(LayerWindow::LayerBackground);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1); // tutto lo schermo, anche sotto la taskbar
    // Le icone del desktop prendono la tastiera quando le clicchi (F2, Canc, Ctrl+C...).
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
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

void setupTaskbarPreview(QQuickWindow* window)
{
    // Le anteprime dei pulsanti: in basso, appena sopra la taskbar (che ha
    // riservato il suo spazio); la distanza dal bordo sinistro la decide il
    // pulsante (ShellController::placeAtLeft). Senza tastiera.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-taskbar-preview"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupSnapLayouts(QQuickWindow* window)
{
    // I layout di snap: tutto lo schermo, trasparente, sopra a tutto (un
    // clic fuori dal pannello lo chiude), con la tastiera per Win+Z.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-snap-layouts"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupSnapAssist(QQuickWindow* window)
{
    // Snap Assist: l'area utile dello schermo (la taskbar resta fuori), così
    // gli spazi in dodicesimi coincidono con quelli delle finestre.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-snap-assist"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityExclusive);
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

    // Testo con FreeType e l'hinting dei font, come Plasma e come Windows:
    // più netto del testo predefinito di Qt Quick (campi di distanza, senza
    // hinting), ai pixel veri dello schermo anche a scala frazionaria.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_DEFAULT_TEXT_RENDER_TYPE")) {
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
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

    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

    Config config;
    // Le icone del tema adatto alla modalità della shell (breeze o
    // breeze-dark); fuori da Plasma Qt potrebbe non conoscere quello scelto.
    applyIconTheme(config.shellTheme() == QLatin1String("light"));
    config.setIconMode(config.shellTheme() == QLatin1String("light") ? QStringLiteral("l/") : QStringLiteral("d/"));

    AppModel apps;
    apps.reload();

    ShellController shell;
    Workspaces workspaces;
    QObject::connect(&shell, &ShellController::workspacesReceived, &workspaces, &Workspaces::update);
    workspaces.query();
    Accessibility accessibility;
    QObject::connect(&shell, &ShellController::accessibilityReceived, &accessibility, &Accessibility::update);
    accessibility.query();
    Network network;
    shell.listen();
    shell.sendWallpaperTint(config.wallpaper());
    QObject::connect(&config, &Config::wallpaperChanged, &shell, [&] { shell.sendWallpaperTint(config.wallpaper()); });
    // La modalità al compositor (tinta acrylic, barre del titolo). Le icone
    // cambiano prima che il QML le richieda (questa connessione viene prima).
    auto sendTheme = [&config] {
        ShellController::sendToCompositor("theme " + config.shellTheme().toLatin1() + ' ' + config.appTheme().toLatin1());
    };
    sendTheme();
    QObject::connect(&config, &Config::themeChanged, &shell, [&config, sendTheme] {
        const bool light = config.shellTheme() == QLatin1String("light");
        applyIconTheme(light);
        sendTheme();
        // Le icone si richiedono quando le cache (anche quella di KIconLoader,
        // che riceve il segnale D-Bus) sono vuote.
        QTimer::singleShot(300, &config, [&config, light] {
            config.setIconMode(light ? QStringLiteral("l/") : QStringLiteral("d/"));
        });
    });
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
    QObject::connect(&shell, &ShellController::filesRequested, &system, [&system] { system.trigger(QStringLiteral("files")); });
    JumpLists jumps(&apps);
    DesktopModel desktop(&apps);
    ServiceMenus serviceMenus;
    FileProperties properties;
    FileActions fileActions;
    Clipboard clipboard;
    QObject::connect(&shell, &ShellController::clipboardClearRequested, &clipboard, &Clipboard::clear);
    // La cronologia accesa o spenta dalle Impostazioni (vela-shell.conf).
    QObject::connect(&config, &Config::clipboardChanged, &clipboard,
        [&config, &clipboard] { clipboard.setEnabled(config.clipboardHistory()); });
    BackgroundEffects effects;
    SystemStatus status;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("fileicon"), new IconProvider(32));
    engine.addImageProvider(QStringLiteral("wallpaper"), new WallpaperProvider);
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider(&capture));
    engine.addImageProvider(QStringLiteral("filethumb"), new FileThumbnailProvider);
    engine.addImageProvider(QStringLiteral("notification"), new NotificationImageProvider(&notifications));
    engine.addImageProvider(QStringLiteral("tray"), new TrayImageProvider(&tray));
    engine.addImageProvider(QStringLiteral("clipboard"), new ClipboardImageProvider(&clipboard));
    Snip snip(&engine, &capture, &clipboard, &notifications);
    engine.addImageProvider(QStringLiteral("snip"), new SnipImageProvider(&snip));
    QObject::connect(&shell, &ShellController::snipRequested, &snip, &Snip::start);
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
    engine.rootContext()->setContextProperty(QStringLiteral("FileActions"), &fileActions);
    engine.rootContext()->setContextProperty(QStringLiteral("Clip"), &clipboard);
    engine.rootContext()->setContextProperty(QStringLiteral("Snip"), &snip);
    engine.rootContext()->setContextProperty(QStringLiteral("Effects"), &effects);
    engine.rootContext()->setContextProperty(QStringLiteral("Status"), &status);
    engine.rootContext()->setContextProperty(QStringLiteral("Access"), &accessibility);
    engine.rootContext()->setContextProperty(QStringLiteral("Network"), &network);

    // Le finestre QML partono invisibili: le trasformiamo in superfici
    // layer-shell PRIMA che vengano mostrate.
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
    engine.loadFromModule("Vela.Shell", "SnapLayouts");
    engine.loadFromModule("Vela.Shell", "SnapAssist");
    engine.loadFromModule("Vela.Shell", "TaskbarPreview");
    engine.loadFromModule("Vela.Shell", "FileDialogs");
    engine.loadFromModule("Vela.Shell", "ClipboardPanel");

    QQuickWindow* switcher = findWindow(engine, "switcher");
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
    QQuickWindow* snapLayouts = findWindow(engine, "snapLayouts");
    QQuickWindow* snapAssist = findWindow(engine, "snapAssist");
    QQuickWindow* taskbarPreview = findWindow(engine, "taskbarPreview");
    QQuickWindow* fileDialogs = findWindow(engine, "fileDialogs");
    QQuickWindow* clipboardPanel = findWindow(engine, "clipboardPanel");
    if (!startMenu || !switcher || !notificationWindow || !contextMenu || !runDialog || !confirmDialog || !sourceChooser || !propertiesDialog || !quickSettings
        || !notificationCenter || !taskView || !desktopOsd || !snapLayouts || !snapAssist || !taskbarPreview || !fileDialogs || !clipboardPanel) {
        qCritical("vela-shell: impossibile caricare l'interfaccia QML");
        return 1;
    }

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
    setupSidePanel(clipboardPanel, QStringLiteral("vela-clipboard"));
    setupTaskView(taskView);
    setupDesktopOsd(desktopOsd);
    setupSnapLayouts(snapLayouts);
    setupSnapAssist(snapAssist);
    setupTaskbarPreview(taskbarPreview);
    setupPropertiesDialog(fileDialogs); // come Proprietà: al centro, sopra le finestre
    LayerWindow::get(fileDialogs)->setScope(QStringLiteral("vela-file-dialogs"));
    // Gli sfondi su ogni schermo; la taskbar anche, come in Windows (o
    // solo sul principale, se l'utente la vuole così).
    ScreenWindows wallpapers(&engine, "Wallpaper", setupWallpaper);
    ScreenWindows taskbars(&engine, "Taskbar", setupTaskbar);
    taskbars.setPrimaryOnly(!config.taskbarAllScreens());
    QObject::connect(&config, &Config::taskbarChanged, &taskbars,
        [&config, &taskbars] { taskbars.setPrimaryOnly(!config.taskbarAllScreens()); });
    if (!wallpapers.start() || !taskbars.start()) {
        return 1;
    }

    return app.exec();
}
