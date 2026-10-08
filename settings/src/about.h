// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>

// System > About, like Windows: the device specifications (name, processor,
// RAM, graphics card...) and the operating system's, and "Rename this PC"
// (systemd-hostnamed, polkit asks for the password).
class About : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hostname READ hostname NOTIFY hostnameChanged)
    Q_PROPERTY(QString userName READ userName CONSTANT) // the full name, otherwise the login
    // [{label, value}]
    Q_PROPERTY(QVariantList device READ device CONSTANT)
    Q_PROPERTY(QVariantList system READ system CONSTANT)
    Q_PROPERTY(QString model READ model CONSTANT) // "Micro-Star MS-7E49", if the firmware says so
    Q_PROPERTY(QString chassisIcon READ chassisIcon CONSTANT)

public:
    explicit About(QObject* parent = nullptr);

    QString hostname() const { return m_hostname; }
    QString userName() const;
    QVariantList device() const { return m_device; }
    QVariantList system() const { return m_system; }
    QString model() const { return m_model; }
    QString chassisIcon() const { return m_chassisIcon; }

    Q_INVOKABLE void rename(const QString& name);
    // The specifications as text, for "Copy".
    Q_INVOKABLE QString asText() const;

signals:
    void hostnameChanged();
    void renameFailed(const QString& message);

private:
    QString m_hostname;
    QVariantList m_device;
    QVariantList m_system;
    QString m_model;
    QString m_chassisIcon;
};
