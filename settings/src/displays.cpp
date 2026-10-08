// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displays.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QtDebug>

Displays::Displays(QObject* parent)
    : QObject(parent)
    , m_available(!QStandardPaths::findExecutable(QStringLiteral("wlr-randr")).isEmpty())
{
    refresh();
}

void Displays::refresh()
{
    if (!m_available) {
        return;
    }
    QProcess process;
    process.start(QStringLiteral("wlr-randr"), { QStringLiteral("--json") });
    if (!process.waitForFinished(2000)) {
        return;
    }
    const QJsonArray heads = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
    QVariantList outputs;
    for (const QJsonValue& value : heads) {
        const QJsonObject head = value.toObject();
        QVariantMap output;
        output[QStringLiteral("name")] = head[QStringLiteral("name")].toString();
        // The name the user sees: make and model, if present.
        QString description = (head[QStringLiteral("make")].toString() + u' ' + head[QStringLiteral("model")].toString()).trimmed();
        output[QStringLiteral("description")] = description.isEmpty() ? head[QStringLiteral("description")].toString() : description;
        output[QStringLiteral("enabled")] = head[QStringLiteral("enabled")].toBool();
        output[QStringLiteral("x")] = head[QStringLiteral("position")].toObject()[QStringLiteral("x")].toInt();
        output[QStringLiteral("y")] = head[QStringLiteral("position")].toObject()[QStringLiteral("y")].toInt();
        output[QStringLiteral("scale")] = head[QStringLiteral("scale")].toDouble();
        output[QStringLiteral("transform")] = head[QStringLiteral("transform")].toString();
        QVariantList modes;
        for (const QJsonValue& m : head[QStringLiteral("modes")].toArray()) {
            const QJsonObject mode = m.toObject();
            const QVariantMap entry {
                { QStringLiteral("width"), mode[QStringLiteral("width")].toInt() },
                { QStringLiteral("height"), mode[QStringLiteral("height")].toInt() },
                { QStringLiteral("refresh"), mode[QStringLiteral("refresh")].toDouble() },
                { QStringLiteral("preferred"), mode[QStringLiteral("preferred")].toBool() },
                { QStringLiteral("current"), mode[QStringLiteral("current")].toBool() },
            };
            if (entry[QStringLiteral("current")].toBool()) {
                output[QStringLiteral("current")] = entry;
            }
            modes.append(entry);
        }
        output[QStringLiteral("modes")] = modes;
        outputs.append(output);
    }
    m_outputs = outputs;
    emit outputsChanged();
}

QStringList Displays::argumentsFor(const QVariantMap& o)
{
    QStringList args { QStringLiteral("--output"), o[QStringLiteral("name")].toString() };
    if (!o.value(QStringLiteral("enabled"), true).toBool()) {
        args << QStringLiteral("--off");
        return args;
    }
    args << QStringLiteral("--on");
    if (o.contains(QStringLiteral("width"))) {
        // Without a known refresh rate (virtual outputs): resolution only.
        const double refresh = o[QStringLiteral("refresh")].toDouble();
        QString mode = QStringLiteral("%1x%2").arg(o[QStringLiteral("width")].toInt()).arg(o[QStringLiteral("height")].toInt());
        if (refresh > 0) {
            mode += QStringLiteral("@%1Hz").arg(refresh, 0, 'f', 3);
        }
        args << QStringLiteral("--mode") << mode;
    }
    if (o.contains(QStringLiteral("scale"))) {
        args << QStringLiteral("--scale") << QString::number(o[QStringLiteral("scale")].toDouble(), 'f', 4);
    }
    if (o.contains(QStringLiteral("transform"))) {
        args << QStringLiteral("--transform") << o[QStringLiteral("transform")].toString();
    }
    if (o.contains(QStringLiteral("x"))) {
        args << QStringLiteral("--pos")
             << QStringLiteral("%1,%2").arg(o[QStringLiteral("x")].toInt()).arg(o[QStringLiteral("y")].toInt());
    }
    return args;
}

bool Displays::run(const QStringList& arguments)
{
    QProcess process;
    process.start(QStringLiteral("wlr-randr"), arguments);
    const bool ok = process.waitForFinished(5000) && process.exitCode() == 0;
    if (!ok) {
        qWarning("vela-settings: wlr-randr %s: %s", qPrintable(arguments.join(u' ')),
            process.readAllStandardError().constData());
    }
    return ok;
}

bool Displays::apply(const QString& name, const QVariantMap& changes)
{
    m_before = m_outputs;
    QVariantMap output { { QStringLiteral("name"), name } };
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        output[it.key()] = it.value();
    }
    const bool ok = run(argumentsFor(output));
    refresh();
    return ok;
}

void Displays::revert()
{
    if (m_before.isEmpty()) {
        return;
    }
    // All outputs as they were, in a single command.
    QStringList args;
    for (const QVariant& value : std::as_const(m_before)) {
        QVariantMap o = value.toMap();
        const QVariantMap current = o[QStringLiteral("current")].toMap();
        if (!current.isEmpty()) {
            o[QStringLiteral("width")] = current[QStringLiteral("width")];
            o[QStringLiteral("height")] = current[QStringLiteral("height")];
            o[QStringLiteral("refresh")] = current[QStringLiteral("refresh")];
        }
        args << argumentsFor(o);
    }
    run(args);
    m_before.clear();
    refresh();
}
