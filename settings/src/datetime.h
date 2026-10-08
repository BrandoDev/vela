// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>

// Time & language > Date & time, with systemd-timedated: time zone and
// automatic sync (NTP). Changing them asks for the password (polkit).
class DateTime : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString timezone READ timezone NOTIFY changed)
    Q_PROPERTY(bool ntp READ ntp NOTIFY changed)
    Q_PROPERTY(bool canNtp READ canNtp NOTIFY changed)
    Q_PROPERTY(QStringList timezones READ timezones CONSTANT)

public:
    explicit DateTime(QObject* parent = nullptr);

    bool available() const { return m_available; }
    QString timezone() const { return m_timezone; }
    bool ntp() const { return m_ntp; }
    bool canNtp() const { return m_canNtp; }
    QStringList timezones() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setNtp(bool on);
    Q_INVOKABLE void setTimezone(const QString& zone);
    // The zone with its UTC offset: "(UTC+01:00) Europe/Rome".
    Q_INVOKABLE QString describe(const QString& zone) const;

signals:
    void changed();

private:
    void call(const QString& method, const QVariantList& arguments);

    bool m_available = false;
    QString m_timezone;
    bool m_ntp = false;
    bool m_canNtp = false;
};
