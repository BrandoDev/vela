#include "accessibility.h"

#include "shellcontroller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>

Accessibility::Accessibility(QObject* parent)
    : QObject(parent)
{
}

void Accessibility::query()
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-") + display + QStringLiteral(".sock"));
    if (!socket.waitForConnected(200)) {
        return;
    }
    socket.write("accessibility\n");
    if (!socket.waitForBytesWritten(200)) {
        return;
    }
    QByteArray reply;
    while (!reply.contains('\n') && socket.waitForReadyRead(200)) {
        reply += socket.readAll();
    }
    update(reply.trimmed());
}

void Accessibility::update(const QByteArray& json)
{
    const QJsonObject state = QJsonDocument::fromJson(json).object();
    if (state.isEmpty()) {
        return;
    }
    m_nightLight = state[QStringLiteral("nightLight")].toBool();
    m_colorFilter = state[QStringLiteral("colorFilter")].toBool();
    m_magnifier = state[QStringLiteral("magnifier")].toBool();
    m_stickyKeys = state[QStringLiteral("stickyKeys")].toBool();
    emit changed();
}

// Si aggiorna subito (il riquadro risponde al clic), poi conferma il compositor.
void Accessibility::setNightLight(bool on)
{
    m_nightLight = on;
    emit changed();
    ShellController::sendToCompositor(on ? "night-light on" : "night-light off");
}

void Accessibility::setColorFilter(bool on)
{
    m_colorFilter = on;
    emit changed();
    ShellController::sendToCompositor(on ? "color-filter on" : "color-filter off");
}

void Accessibility::setMagnifier(bool on)
{
    m_magnifier = on;
    emit changed();
    ShellController::sendToCompositor(on ? "magnifier on" : "magnifier off");
}

void Accessibility::setStickyKeys(bool on)
{
    m_stickyKeys = on;
    emit changed();
    ShellController::sendToCompositor(on ? "sticky-keys on" : "sticky-keys off");
}
