// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// vela-shell: Vela's taskbar and Start menu.
//
// Every shell piece is a "layer-shell" window: the compositor knows it's part
// of the desktop (not an app), keeps it above or below the windows and
// reserves space for it on the output.

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
#include "language.h"
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
    layer->setScreen(screen); // one per output (ScreenWindows)
    layer->setScope(QStringLiteral("vela-taskbar"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft
        | LayerWindow::AnchorRight);
    layer->setExclusiveZone(window->height()); // maximized windows don't cover it
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupWallpaper(QQuickWindow* window, QScreen* screen)
{
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScreen(screen); // this very output, not the one the compositor would choose
    layer->setScope(QStringLiteral("vela-wallpaper"));
    layer->setLayer(LayerWindow::LayerBackground);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
        | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
    layer->setExclusiveZone(-1); // the whole output, also below the taskbar
    // Desktop icons take the keyboard when clicked (F2, Del, Ctrl+C...).
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupSwitcher(QQuickWindow* window)
{
    // Fullscreen and transparent: the panel sits in the center. It doesn't
    // take the keyboard, which stays with the compositor (it handles Alt+Tab).
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
    // Anchored only at the bottom: the compositor centers it horizontally and
    // puts it above the taskbar (which reserved its space). No margin: the
    // window touches the taskbar, so the rising panel is cut there and seems
    // to come out from behind the taskbar, like on Windows 11.
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom));
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

// With the taskbar aligned left, Start opens at the bottom left.
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
    // At the bottom right, above the taskbar (which reserved its space) and
    // above the windows. It doesn't take the keyboard.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-notifications"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorRight);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupContextMenu(QQuickWindow* window)
{
    // Right-click menus: the whole output, transparent, above everything. The
    // menus sit inside, and a click outside closes them. It takes the keyboard
    // (arrows, Enter, Esc) and loses it when clicking elsewhere.
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
    // "Run", at the bottom left above the taskbar, like in Windows.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-run"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft);
    layer->setMargins(QMargins(12, 0, 0, 12));
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupConfirmDialog(QQuickWindow* window)
{
    // Questions (such as "Empty Recycle Bin"): in the center of the output,
    // above the windows.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-confirm"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupSourceChooser(QQuickWindow* window)
{
    // What to share: in the center of the main output, above everything.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-share"));
    layer->setLayer(LayerWindow::LayerOverlay);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupPropertiesDialog(QQuickWindow* window)
{
    // Properties: in the center of the output, above the windows like a
    // dialog.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-properties"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors());
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

void setupTaskView(QQuickWindow* window)
{
    // Task View: all the space above the taskbar (which stays visible and
    // clickable, like in Windows), with the keyboard.
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
    // The desktop name in the center of the output: without anchors the
    // compositor centers it. It takes neither keyboard nor clicks.
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
    // Button previews: at the bottom, just above the taskbar (which reserved
    // its space); the distance from the left edge is decided by the button
    // (ShellController::placeAtLeft). No keyboard.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(QStringLiteral("vela-taskbar-preview"));
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorLeft);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
}

void setupSnapLayouts(QQuickWindow* window)
{
    // Snap layouts: the whole output, transparent, above everything (a click
    // outside the panel closes it), with the keyboard for Win+Z.
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
    // Snap Assist: the output's usable area (the taskbar stays out), so the
    // spaces in twelfths match the windows'.
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
    // Quick settings and notification center: at the bottom right, above the
    // taskbar (which reserved its space), above the windows.
    LayerWindow* layer = LayerWindow::get(window);
    layer->setScope(scope);
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorBottom) | LayerWindow::AnchorRight);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
}

} // namespace

int main(int argc, char* argv[])
{
    // "vela-shell --choose-source": the chooser xdg-desktop-portal-wlr
    // launches when an app wants to share the screen. It asks the already open
    // shell, which shows the choice, and prints the answer for the portal.
    if (argc > 1 && QByteArray(argv[1]) == "--choose-source") {
        QCoreApplication app(argc, argv);
        const QByteArray answer = ShellController::askRunningInstance("choose-source");
        if (!answer.isEmpty()) {
            std::printf("%s\n", answer.constData());
        }
        return 0;
    }

    // Text with FreeType and font hinting, like Plasma and Windows: crisper
    // than Qt Quick's default text (distance fields, no hinting), on the
    // output's real pixels even at fractional scales.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_DEFAULT_TEXT_RENDER_TYPE")) {
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("vela-shell"));
    QGuiApplication::setOrganizationName(QStringLiteral("Vela"));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    // Before anything else: the C++ models are born already translated too.
    QQmlApplicationEngine* qmlEngine = nullptr;
    AppModel* appModel = nullptr;
    DesktopModel* desktopModel = nullptr;
    vela::language::install(QStringLiteral("vela-shell"), [&qmlEngine, &appModel, &desktopModel] {
        if (appModel) {
            appModel->reload(); // app names in the new language (Name[en], Name[it])
        }
        if (desktopModel) {
            desktopModel->refresh(); // the Recycle Bin
        }
        if (qmlEngine) {
            qmlEngine->retranslate();
        }
    });

    // "vela-shell --toggle-start": handy for shortcuts and scripts.
    const QStringList args = QGuiApplication::arguments();
    if (args.contains(QStringLiteral("--toggle-start"))) {
        return ShellController::sendToRunningInstance("toggle-start") ? 0 : 1;
    }
    if (ShellController::sendToRunningInstance("ping")) {
        qWarning("vela-shell: already running in this session");
        return 0;
    }

    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        qWarning("vela-shell: needs a Wayland session (current platform: %s)",
            qPrintable(QGuiApplication::platformName()));
        return 1;
    }

    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

    Config config;
    // Icons from the theme matching the shell's mode (breeze or breeze-dark);
    // outside Plasma Qt might not know the chosen one.
    applyIconTheme(config.shellTheme() == QLatin1String("light"));
    config.setIconMode(config.shellTheme() == QLatin1String("light") ? QStringLiteral("l/") : QStringLiteral("d/"));

    AppModel apps;
    appModel = &apps;
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
    // The mode to the compositor (acrylic tint, title bars). Icons change
    // before QML asks for them (this connection comes first).
    auto sendTheme = [&config] {
        ShellController::sendToCompositor("theme " + config.shellTheme().toLatin1() + ' ' + config.appTheme().toLatin1());
    };
    sendTheme();
    QObject::connect(&config, &Config::themeChanged, &shell, [&config, sendTheme] {
        const bool light = config.shellTheme() == QLatin1String("light");
        applyIconTheme(light);
        sendTheme();
        // Icons are asked for once the caches (KIconLoader's too, which gets
        // the D-Bus signal) are empty.
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
    desktopModel = &desktop;
    ServiceMenus serviceMenus;
    FileProperties properties;
    FileActions fileActions;
    Clipboard clipboard;
    QObject::connect(&shell, &ShellController::clipboardClearRequested, &clipboard, &Clipboard::clear);
    // History turned on or off from Settings (vela-shell.conf).
    QObject::connect(&config, &Config::clipboardChanged, &clipboard,
        [&config, &clipboard] { clipboard.setEnabled(config.clipboardHistory()); });
    BackgroundEffects effects;
    SystemStatus status;

    QQmlApplicationEngine engine;
    qmlEngine = &engine;
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

    // QML windows start invisible: we turn them into layer-shell surfaces
    // BEFORE they're shown.
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
        qCritical("vela-shell: can't load the QML interface");
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
    setupPropertiesDialog(fileDialogs); // like Properties: in the center, above the windows
    LayerWindow::get(fileDialogs)->setScope(QStringLiteral("vela-file-dialogs"));
    // Wallpapers on every output; the taskbar too, like in Windows (or only on
    // the main one, if the user wants that).
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
