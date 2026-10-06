#include "snip.h"

#include "clipboard.h"
#include "notifications.h"
#include "windowcapture.h"

#include <LayerShellQt/Window>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

// Le finestre visibili, chieste al compositor.
QVariantList queryWindows()
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-") + display + QStringLiteral(".sock"));
    if (!socket.waitForConnected(200)) {
        return {};
    }
    socket.write("window-rects\n");
    socket.waitForBytesWritten(200);
    QByteArray reply;
    while (!reply.contains('\n') && socket.waitForReadyRead(200)) {
        reply += socket.readAll();
    }
    return QJsonDocument::fromJson(reply.trimmed()).array().toVariantList();
}

} // namespace

Snip::Snip(QQmlEngine* engine, WindowCapture* capture, Clipboard* clipboard, NotificationServer* notifications,
    QObject* parent)
    : QObject(parent)
    , m_component(engine, "Vela.Shell", "SnipOverlay")
    , m_capture(capture)
    , m_clipboard(clipboard)
    , m_notifications(notifications)
{
    connect(m_capture, &WindowCapture::screenCaptured, this, [this](const QString&, bool) {
        if (m_waiting > 0 && --m_waiting == 0) {
            showOverlays();
        }
    });
    // Un clic sulla notifica: si apre lo screenshot.
    connect(m_notifications, &NotificationServer::ActionInvoked, this, [this](uint id, const QString&) {
        const QString path = m_saved.take(id);
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
}

void Snip::setMode(const QString& mode)
{
    if (mode != m_mode) {
        m_mode = mode;
        emit modeChanged();
    }
}

QImage Snip::image(const QString& screen) const
{
    return m_capture->fullImage(screen);
}

void Snip::start()
{
    if (m_active) {
        return;
    }
    if (m_component.isError()) {
        qWarning() << "vela-shell: strumento di cattura" << m_component.errors();
        return;
    }
    m_active = true;
    m_mode = QStringLiteral("rect");
    m_windows = queryWindows();
    m_capture->clearFull();
    m_waiting = m_capture->captureScreensFull();
    emit modeChanged();
    emit activeChanged();
    if (m_waiting == 0) {
        cancel();
        return;
    }
    // Se una cattura non torna, si va avanti con quelle che ci sono.
    QTimer::singleShot(1500, this, [this] {
        if (m_waiting > 0) {
            m_waiting = 0;
            showOverlays();
        }
    });
}

void Snip::showOverlays()
{
    for (QScreen* screen : QGuiApplication::screens()) {
        if (m_capture->fullImage(screen->name()).isNull()) {
            continue;
        }
        QObject* object = m_component.create();
        auto* window = qobject_cast<QQuickWindow*>(object);
        if (!window) {
            qWarning() << "vela-shell: strumento di cattura" << m_component.errors();
            delete object;
            continue;
        }
        window->setProperty("screenName", screen->name());
        window->setScreen(screen);
        using LayerWindow = LayerShellQt::Window;
        LayerWindow* layer = LayerWindow::get(window);
        layer->setScreen(screen);
        layer->setScope(QStringLiteral("vela-snip"));
        layer->setLayer(LayerWindow::LayerOverlay);
        layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
            | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityOnDemand);
        window->show();
        m_overlays.append(window);
    }
    if (m_overlays.isEmpty()) {
        cancel();
    }
}

void Snip::closeOverlays()
{
    for (const QPointer<QQuickWindow>& window : std::as_const(m_overlays)) {
        if (window) {
            window->hide();
            window->deleteLater();
        }
    }
    m_overlays.clear();
}

void Snip::cancel()
{
    if (!m_active) {
        return;
    }
    closeOverlays();
    m_capture->clearFull();
    m_active = false;
    emit activeChanged();
}

void Snip::finish(const QString& screenName, qreal x, qreal y, qreal width, qreal height)
{
    const QImage full = m_capture->fullImage(screenName);
    QScreen* screen = nullptr;
    for (QScreen* candidate : QGuiApplication::screens()) {
        if (candidate->name() == screenName) {
            screen = candidate;
        }
    }
    if (full.isNull() || !screen || width < 1 || height < 1) {
        cancel();
        return;
    }
    // Da coordinate logiche ai pixel dell'immagine (la scala dello schermo).
    const qreal sx = full.width() / qreal(screen->geometry().width());
    const qreal sy = full.height() / qreal(screen->geometry().height());
    const QRect area = QRect(qRound(x * sx), qRound(y * sy), qRound(width * sx), qRound(height * sy))
                           .intersected(full.rect());
    const QImage cut = full.copy(area);
    closeOverlays();
    m_capture->clearFull();
    m_active = false;
    emit activeChanged();
    if (cut.isNull()) {
        return;
    }

    m_clipboard->copyImage(cut);
    // In Immagini/Screenshot, come Windows.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshot");
    QDir().mkpath(folder);
    const QString path = folder + QStringLiteral("/Screenshot ")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss")) + QStringLiteral(".png");
    const bool saved = cut.save(path, "PNG");
    const uint id = m_notifications->Notify(QStringLiteral("Strumento di cattura"), 0,
        QStringLiteral("accessories-screenshot"), QStringLiteral("Screenshot copiato negli Appunti"),
        saved ? QStringLiteral("Salvato in Immagini > Screenshot. Fai clic per aprirlo.")
              : QStringLiteral("Non è stato possibile salvarlo in Immagini > Screenshot."),
        saved ? QStringList { QStringLiteral("default"), QStringLiteral("Apri") } : QStringList(),
        QVariantMap { { QStringLiteral("image-path"), path } }, 6000);
    if (saved) {
        m_saved.insert(id, path);
    }
}
