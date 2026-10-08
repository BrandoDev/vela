// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mediakeys.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QHash>

namespace {

const QString mprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString mprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

// "Playing", "Paused" or "Stopped"; empty if the player doesn't answer in time
// (a hung player mustn't freeze the shell).
QString playbackStatus(const QString& service)
{
    QDBusMessage get = QDBusMessage::createMethodCall(service, mprisPath, propertiesInterface, QStringLiteral("Get"));
    get << playerInterface << QStringLiteral("PlaybackStatus");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(get, QDBus::Block, 300);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return QString();
    }
    return reply.arguments().first().value<QDBusVariant>().variant().toString();
}

} // namespace

MediaKeys::MediaKeys(QObject* parent)
    : QObject(parent)
{
    // Every player's changes (any sender): which one started playing.
    QDBusConnection::sessionBus().connect(QString(), mprisPath, propertiesInterface, QStringLiteral("PropertiesChanged"),
        this, SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
}

void MediaKeys::onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList&)
{
    if (interface == playerInterface && changed.value(QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing")) {
        m_lastPlaying = message().service();
    }
}

QString MediaKeys::target() const
{
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        return QString();
    }
    QString playing, last, paused, any;
    for (const QString& name : bus->registeredServiceNames().value()) {
        if (!name.startsWith(mprisPrefix)) {
            continue;
        }
        const bool wasLast = !m_lastPlaying.isEmpty() && bus->serviceOwner(name).value() == m_lastPlaying;
        const QString status = playbackStatus(name);
        if (status == QLatin1String("Playing") && (playing.isEmpty() || wasLast)) {
            playing = name;
        }
        if (wasLast) {
            last = name;
        }
        if (status == QLatin1String("Paused") && paused.isEmpty()) {
            paused = name;
        }
        if (any.isEmpty()) {
            any = name;
        }
    }
    for (const QString& name : { playing, last, paused, any }) {
        if (!name.isEmpty()) {
            return name;
        }
    }
    return QString();
}

void MediaKeys::press(const QString& key)
{
    static const QHash<QString, QString> methods {
        { QStringLiteral("play-pause"), QStringLiteral("PlayPause") },
        { QStringLiteral("pause"), QStringLiteral("Pause") },
        { QStringLiteral("stop"), QStringLiteral("Stop") },
        { QStringLiteral("next"), QStringLiteral("Next") },
        { QStringLiteral("previous"), QStringLiteral("Previous") },
    };
    const QString method = methods.value(key);
    const QString service = method.isEmpty() ? QString() : target();
    if (service.isEmpty()) {
        return;
    }
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(service, mprisPath, playerInterface, method));
}
