// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVariantMap>

#include <csignal>
#include <sys/prctl.h>

namespace {

QByteArray pactl(const QStringList& arguments)
{
    QProcess process;
    process.start(QStringLiteral("pactl"), arguments);
    process.waitForFinished(2000);
    return process.readAllStandardOutput();
}

QString sinkOrSource(const QString& kind)
{
    return kind == QLatin1String("input") ? QStringLiteral("source") : QStringLiteral("sink");
}

} // namespace

Audio::Audio(QObject* parent)
    : QObject(parent)
    , m_available(!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
{
    if (!m_available) {
        return;
    }
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_debounce, &QTimer::timeout, this, &Audio::refresh);
    connect(&m_subscribe, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray events = m_subscribe.readAllStandardOutput();
        if (events.contains("sink") || events.contains("source") || events.contains("server")) {
            m_debounce.start();
        }
    });
}

Audio::~Audio()
{
    m_subscribe.kill();
    m_subscribe.waitForFinished(500);
}

QVariantList Audio::list(const QString& what, const QString& defaultName) const
{
    QVariantList out;
    const QJsonArray devices = QJsonDocument::fromJson(pactl({ QStringLiteral("-f"), QStringLiteral("json"),
                                                                QStringLiteral("list"), what }))
                                   .array();
    for (const QJsonValue& value : devices) {
        const QJsonObject device = value.toObject();
        const QString name = device[QStringLiteral("name")].toString();
        // I "monitor" delle uscite non sono microfoni.
        if (what == QLatin1String("sources") && name.endsWith(QLatin1String(".monitor"))) {
            continue;
        }
        // Il volume: la media dei canali (65536 = 100%).
        const QJsonObject channels = device[QStringLiteral("volume")].toObject();
        double sum = 0.0;
        for (const QJsonValue& channel : channels) {
            sum += channel.toObject()[QStringLiteral("value")].toDouble();
        }
        const double volume = channels.isEmpty() ? 0.0 : sum / channels.size() / 65536.0;
        out.append(QVariantMap {
            { QStringLiteral("name"), name },
            { QStringLiteral("description"), device[QStringLiteral("description")].toString() },
            { QStringLiteral("volume"), volume },
            { QStringLiteral("muted"), device[QStringLiteral("mute")].toBool() },
            { QStringLiteral("isDefault"), name == defaultName },
        });
    }
    return out;
}

void Audio::refresh()
{
    if (!m_available) {
        return;
    }
    // La prima volta che la pagina si apre: da lì in poi si aggiorna da sé.
    if (m_subscribe.state() == QProcess::NotRunning) {
        // Se usciamo di colpo (crash), pactl non deve restare orfano.
        m_subscribe.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
        m_subscribe.start(QStringLiteral("pactl"), { QStringLiteral("subscribe") });
    }
    const QString defaultSink = QString::fromUtf8(pactl({ QStringLiteral("get-default-sink") })).trimmed();
    const QString defaultSource = QString::fromUtf8(pactl({ QStringLiteral("get-default-source") })).trimmed();
    m_outputs = list(QStringLiteral("sinks"), defaultSink);
    m_inputs = list(QStringLiteral("sources"), defaultSource);
    emit changed();
}

void Audio::setDefault(const QString& kind, const QString& name)
{
    QProcess::startDetached(QStringLiteral("pactl"), { QStringLiteral("set-default-") + sinkOrSource(kind), name });
}

void Audio::setVolume(const QString& kind, const QString& name, double volume)
{
    const int percent = qRound(qBound(0.0, volume, 1.5) * 100.0);
    QProcess::startDetached(QStringLiteral("pactl"),
        { QStringLiteral("set-") + sinkOrSource(kind) + QStringLiteral("-volume"), name, QString::number(percent) + u'%' });
}

void Audio::setMuted(const QString& kind, const QString& name, bool muted)
{
    QProcess::startDetached(QStringLiteral("pactl"),
        { QStringLiteral("set-") + sinkOrSource(kind) + QStringLiteral("-mute"), name,
            muted ? QStringLiteral("1") : QStringLiteral("0") });
}
