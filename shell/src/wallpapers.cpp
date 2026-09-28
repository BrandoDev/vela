#include "wallpapers.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QtDebug>

namespace {

void setupWallpaper(QQuickWindow* window, QScreen* screen)
{
    using LayerWindow = LayerShellQt::Window;
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

bool realScreen(const QScreen* screen)
{
    return screen && !screen->name().isEmpty(); // senza nome: il segnaposto di Qt
}

} // namespace

Wallpapers::Wallpapers(QQmlEngine* engine, QObject* parent)
    : QObject(parent)
    , m_component(engine, "Vela.Shell", "Wallpaper")
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(100); // gli schermi arrivano uno alla volta
    connect(&m_syncTimer, &QTimer::timeout, this, &Wallpapers::sync);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
        m_retries = 0;
        scheduleSync();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &Wallpapers::remove);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &Wallpapers::scheduleSync);
}

bool Wallpapers::start()
{
    if (m_component.isError()) {
        qCritical() << "vela-shell: sfondo" << m_component.errors();
        return false;
    }
    sync();
    return true;
}

void Wallpapers::scheduleSync()
{
    m_syncTimer.start();
}

void Wallpapers::remove(QScreen* screen)
{
    if (QPointer<QQuickWindow> window = m_windows.take(screen)) {
        window->deleteLater();
    }
}

void Wallpapers::sync()
{
    QScreen* primary = QGuiApplication::primaryScreen();
    for (QScreen* screen : QGuiApplication::screens()) {
        if (!realScreen(screen)) {
            continue;
        }
        QPointer<QQuickWindow>& window = m_windows[screen];
        if (!window) {
            QObject* object = m_component.create();
            window = qobject_cast<QQuickWindow*>(object);
            if (!window) {
                delete object;
                m_windows.remove(screen);
                continue;
            }
            static_cast<QObject*>(window)->setParent(this); // proprietà, non finestra madre
            qInfo("vela-shell: sfondo su %s%s", qPrintable(screen->name()), screen == primary ? " (principale)" : "");
            window->setScreen(screen);
            setupWallpaper(window, screen);
            // Chiuso dal compositor: lo si rimette, ma senza insistere se
            // continua a chiuderlo.
            connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
                if (!visible && m_retries < 5) {
                    ++m_retries;
                    scheduleSync();
                }
            });
        }
        window->setProperty("primary", screen == primary);
        if (!window->isVisible()) {
            window->setScreen(screen);
            window->show();
        }
    }
}
