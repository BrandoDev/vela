// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Turning Bluetooth on and off (quick settings and Settings).
//
// BlueZ doesn't power on an adapter blocked by rfkill: "Powered" stays false
// and PowerState says "off-blocked". Plasma, for example, sets the block when
// it turns Bluetooth off. To turn it on, the soft block is removed first by
// writing to /dev/rfkill (the session user can: systemd's uaccess rule), then
// BlueZ is asked to power it on; if the unblock hasn't reached BlueZ yet, it's
// retried a moment later.

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

// Removes the soft block from all Bluetooth devices.
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
        // BlueZ sees the unblock with some delay: until the adapter is on,
        // it's asked again (at most three times).
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
