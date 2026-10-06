// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Accendere e spegnere il Bluetooth (impostazioni rapide e Impostazioni).
//
// BlueZ non accende un adattatore bloccato da rfkill: "Powered" resta
// falso e PowerState dice "off-blocked". Il blocco lo mette, per esempio,
// Plasma quando spegne il Bluetooth. Per accenderlo si toglie prima il
// blocco software scrivendo in /dev/rfkill (l'utente della sessione può:
// regola uaccess di systemd), poi si chiede a BlueZ di accenderlo; se lo
// sblocco non è ancora arrivato a BlueZ, si riprova dopo un attimo.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <fcntl.h>
#include <linux/rfkill.h>
#include <unistd.h>

namespace vela::bluetooth {

// Toglie il blocco software di tutti i dispositivi Bluetooth.
inline bool unblock()
{
    const int fd = ::open("/dev/rfkill", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    rfkill_event event {};
    event.op = RFKILL_OP_CHANGE_ALL;
    event.type = RFKILL_TYPE_BLUETOOTH;
    event.soft = 0;
    const bool ok = ::write(fd, &event, RFKILL_EVENT_SIZE_V1) == RFKILL_EVENT_SIZE_V1;
    ::close(fd);
    return ok;
}

inline void setPowered(const QString& adapter, bool on, int attempt = 0)
{
    if (adapter.isEmpty()) {
        return;
    }
    if (on && attempt == 0) {
        unblock();
    }
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), adapter,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    call << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered") << QVariant::fromValue(QDBusVariant(on));
    QDBusConnection::systemBus().callWithCallback(call, nullptr, nullptr, nullptr);
    if (on && attempt < 3) {
        // BlueZ vede lo sblocco con un po' di ritardo: finché l'adattatore
        // non è acceso, si richiede (al massimo tre volte).
        QTimer::singleShot(400, [adapter, attempt] {
            QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), adapter,
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
            get << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered");
            const QDBusMessage reply = QDBusConnection::systemBus().call(get, QDBus::Block, 500);
            const bool powered = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
                && reply.arguments().first().value<QDBusVariant>().variant().toBool();
            if (!powered) {
                setPowered(adapter, true, attempt + 1);
            }
        });
    }
}

} // namespace vela::bluetooth
