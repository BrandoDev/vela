// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "datetime.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDateTime>
#include <QTimeZone>

namespace {

const QString timedated = QStringLiteral("org.freedesktop.timedate1");
const QString timedatedPath = QStringLiteral("/org/freedesktop/timedate1");

} // namespace

DateTime::DateTime(QObject* parent)
    : QObject(parent)
{
    refresh();
}

void DateTime::refresh()
{
    QDBusInterface timedate(timedated, timedatedPath, timedated, QDBusConnection::systemBus());
    m_available = timedate.isValid();
    m_timezone = timedate.property("Timezone").toString();
    if (m_timezone.isEmpty()) {
        m_timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    }
    m_ntp = timedate.property("NTP").toBool();
    m_canNtp = timedate.property("CanNTP").toBool();
    emit changed();
}

QStringList DateTime::timezones() const
{
    QStringList out;
    for (const QByteArray& id : QTimeZone::availableTimeZoneIds()) {
        // Only "Continent/City" names, like timedatectl list-timezones.
        if (id.contains('/') && !id.startsWith("Etc/") && !id.startsWith("SystemV/") && !id.startsWith("posix/")
            && !id.startsWith("right/")) {
            out << QString::fromUtf8(id);
        }
    }
    return out;
}

QString DateTime::describe(const QString& zone) const
{
    const QTimeZone tz(zone.toUtf8());
    if (!tz.isValid()) {
        return zone;
    }
    const int offset = tz.offsetFromUtc(QDateTime::currentDateTimeUtc());
    const int hours = std::abs(offset) / 3600;
    const int minutes = std::abs(offset) % 3600 / 60;
    return QStringLiteral("(UTC%1%2:%3) %4")
        .arg(offset < 0 ? u'-' : u'+')
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(QString(zone).replace(u'_', u' '));
}

void DateTime::call(const QString& method, const QVariantList& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(timedated, timedatedPath, timedated, method);
    message.setArguments(arguments);
    message.setInteractiveAuthorizationAllowed(true);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        refresh(); // whether it worked or not, the page shows how things really are
    });
}

void DateTime::setNtp(bool on)
{
    call(QStringLiteral("SetNTP"), { on, true });
}

void DateTime::setTimezone(const QString& zone)
{
    call(QStringLiteral("SetTimezone"), { zone, true });
}
