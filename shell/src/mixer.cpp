// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mixer.h"

#include "appmodel.h"
#include "volumesteps.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QByteArray output(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(1000)) {
        return {};
    }
    return process.readAllStandardOutput();
}

QJsonArray pactlList(const QString& what)
{
    return QJsonDocument::fromJson(output(QStringLiteral("pactl"), { QStringLiteral("-f"), QStringLiteral("json"),
                                                                       QStringLiteral("list"), what }))
        .array();
}

// The channels' average (65536 = 100), on the scale people see.
double averageVolume(const QJsonObject& volume)
{
    double sum = 0.0;
    for (const QJsonValue& channel : volume) {
        sum += channel[QStringLiteral("value")].toDouble();
    }
    return volume.isEmpty() ? 0.0 : sum / volume.size() / 65536.0;
}

QString percent(double volume)
{
    return QString::number(qRound(qBound(0.0, volume, 1.0) * 100.0)) + u'%';
}

// The streams sent to a chosen output, not the default one: "target.object"
// in PipeWire's metadata (node id -> the output's id, serial or name; -1:
// the default one again). Read only.
QHash<QString, QString> streamTargets()
{
    QHash<QString, QString> targets;
    static const QRegularExpression line(QStringLiteral(R"re(id:(\d+) key:'target\.object' value:'([^']*)')re"));
    const QString text = QString::fromUtf8(output(QStringLiteral("pw-metadata"), { QStringLiteral("-n"), QStringLiteral("default") }));
    for (auto match = line.globalMatch(text); match.hasNext();) {
        const QRegularExpressionMatch m = match.next();
        if (m.captured(2) != QLatin1String("-1") && !m.captured(2).isEmpty()) {
            targets.insert(m.captured(1), m.captured(2));
        }
    }
    return targets;
}

// A Bluetooth headset's charge, as it reports it to BlueZ (Battery1, the same
// source as UPower and the other desktops); -1 if it doesn't.
int bluetoothBattery(const QString& path)
{
    QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get << QStringLiteral("org.bluez.Battery1") << QStringLiteral("Percentage");
    const QDBusMessage reply = QDBusConnection::systemBus().call(get, QDBus::Block, 300);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return -1;
    }
    bool ok = false;
    const int value = reply.arguments().first().value<QDBusVariant>().variant().toInt(&ok);
    return ok && value >= 0 && value <= 100 ? value : -1;
}

} // namespace

Mixer::Mixer(AppModel* apps, QObject* parent)
    : QObject(parent)
    , m_appModel(apps)
    , m_available(!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80); // events come in bursts
    connect(&m_debounce, &QTimer::timeout, this, &Mixer::refresh);
    connect(&m_runner, &QProcess::finished, this, &Mixer::runNext);
    connect(&m_runner, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            runNext(); // no "finished" will come
        }
    });
}

QVariantList Mixer::apps() const
{
    QVariantList out;
    for (const QVariantMap& app : m_apps) {
        out.append(app);
    }
    return out;
}

void Mixer::setActive(bool active)
{
    m_active = active;
    if (active) {
        refresh();
    }
}

void Mixer::onAudioEvents(const QByteArray& lines)
{
    if (m_active && (lines.contains("sink") || lines.contains("server") || lines.contains("card"))) {
        m_debounce.start();
    }
}

void Mixer::refresh()
{
    // Our own change is still on its way: what pactl reads now is older (the
    // slider would jump back). The last command reads again when it's done.
    if (m_active && !busy()) {
        load();
    }
}

void Mixer::load()
{
    if (!m_available) {
        return;
    }
    m_loaded.start();
    const QString defaultName = QString::fromUtf8(output(QStringLiteral("pactl"), { QStringLiteral("get-default-sink") })).trimmed();

    // Each output's name by its index and serial: how PipeWire names the
    // output a stream was sent to.
    QHash<QString, QString> sinkNames;
    QHash<int, QString> sinkByIndex;
    QHash<QString, QString> descriptions; // unplugged ones too
    m_outputs.clear();
    for (const QJsonValue& value : pactlList(QStringLiteral("sinks"))) {
        const QJsonObject sink = value.toObject();
        const QString name = sink[QStringLiteral("name")].toString();
        const QJsonObject properties = sink[QStringLiteral("properties")].toObject();
        const int index = sink[QStringLiteral("index")].toInt();
        sinkByIndex.insert(index, name);
        descriptions.insert(name, sink[QStringLiteral("description")].toString());
        sinkNames.insert(name, name);
        sinkNames.insert(properties[QStringLiteral("object.id")].toString(), name);
        // The serial last: it's what the target holds, and pactl's index.
        sinkNames.insert(QString::number(index), name);
        sinkNames.insert(properties[QStringLiteral("object.serial")].toString(), name);
        // Like Windows, what isn't plugged in isn't offered (headphones, a
        // screen without speakers).
        const QString activePort = sink[QStringLiteral("active_port")].toString();
        bool unplugged = false;
        for (const QJsonValue& port : sink[QStringLiteral("ports")].toArray()) {
            if (port[QStringLiteral("name")].toString() == activePort) {
                unplugged = port[QStringLiteral("availability")].toString() == QLatin1String("not available");
            }
        }
        if (unplugged && name != defaultName) {
            continue;
        }
        const QString icon = properties[QStringLiteral("device.icon_name")].toString();
        // A Bluetooth headset: its BlueZ object, given by PipeWire or made
        // from its address.
        int battery = -1;
        const QString address = properties[QStringLiteral("api.bluez5.address")].toString();
        if (!address.isEmpty()) {
            QString path = properties[QStringLiteral("api.bluez5.path")].toString();
            if (path.isEmpty()) {
                path = QStringLiteral("/org/bluez/hci0/dev_") + QString(address).replace(u':', u'_');
            }
            battery = bluetoothBattery(path);
        }
        m_outputs.append(QVariantMap {
            { QStringLiteral("name"), name },
            { QStringLiteral("description"), sink[QStringLiteral("description")].toString() },
            { QStringLiteral("icon"), icon.isEmpty() ? QStringLiteral("audio-speakers") : icon },
            { QStringLiteral("isDefault"), name == defaultName },
            { QStringLiteral("battery"), battery },
        });
    }

    // The apps: their streams, grouped by program.
    const QHash<QString, QString> targets = streamTargets();
    m_apps.clear();
    for (const QJsonValue& value : pactlList(QStringLiteral("sink-inputs"))) {
        const QJsonObject stream = value.toObject();
        const QJsonObject properties = stream[QStringLiteral("properties")].toObject();
        const auto property = [&](const char* key) { return properties[QLatin1String(key)].toString(); };
        const QString binary = property("application.process.binary");
        const QString appName = property("application.name");
        const QString key = !binary.isEmpty() ? binary : !appName.isEmpty() ? appName : property("node.name");
        const int index = stream[QStringLiteral("index")].toInt();
        if (QVariantMap* app = findApp(key)) {
            app->insert(QStringLiteral("streams"), app->value(QStringLiteral("streams")).toList() << index);
            continue;
        }
        // The name and icon of its .desktop file, if we find it.
        QString name = appName.isEmpty() ? key : appName;
        QString icon = property("application.icon_name");
        QString desktopId;
        for (const QString& id : { property("pipewire.access.portal.app_id"), property("application.id"), binary, appName }) {
            desktopId = m_appModel ? m_appModel->findDesktopId(id) : QString();
            if (!desktopId.isEmpty()) {
                const QVariantMap entry = m_appModel->entry(desktopId);
                name = entry.value(QStringLiteral("name")).toString();
                icon = entry.value(QStringLiteral("iconName")).toString();
                break;
            }
        }
        // Sent to a chosen output: the one it's on now (the chosen one may be
        // unplugged, then the sound goes to the default one).
        const bool chosen = targets.contains(property("object.id"));
        QString target = sinkNames.value(targets.value(property("object.id")));
        if (chosen && target.isEmpty()) {
            target = sinkByIndex.value(stream[QStringLiteral("sink")].toInt());
        }
        m_apps.append(QVariantMap {
            { QStringLiteral("key"), key },
            { QStringLiteral("name"), name },
            { QStringLiteral("icon"), icon.isEmpty() ? QStringLiteral("applications-multimedia") : icon },
            { QStringLiteral("desktopId"), desktopId },
            { QStringLiteral("volume"), averageVolume(stream[QStringLiteral("volume")].toObject()) },
            { QStringLiteral("muted"), stream[QStringLiteral("mute")].toBool() },
            { QStringLiteral("output"), chosen ? target : QString() },
            { QStringLiteral("outputDescription"), chosen ? descriptions.value(target, target) : QString() },
            { QStringLiteral("streams"), QVariantList { index } },
        });
    }
    emit changed();
}

QVariantMap* Mixer::findApp(const QString& key)
{
    for (QVariantMap& app : m_apps) {
        if (app.value(QStringLiteral("key")).toString() == key) {
            return &app;
        }
    }
    return nullptr;
}

void Mixer::setDefaultOutput(const QString& name)
{
    run(QStringLiteral("default"), { QStringLiteral("set-default-sink"), name });
    for (QVariant& output : m_outputs) {
        QVariantMap map = output.toMap();
        map.insert(QStringLiteral("isDefault"), map.value(QStringLiteral("name")).toString() == name);
        output = map;
    }
    emit changed();
}

void Mixer::setAppVolume(const QString& key, double volume)
{
    QVariantMap* app = findApp(key);
    if (!app) {
        return;
    }
    volume = VolumeSteps::snap(volume, VolumeSteps::evenGrid);
    app->insert(QStringLiteral("volume"), volume); // at once: the slider doesn't wait for pactl
    for (const QVariant& stream : app->value(QStringLiteral("streams")).toList()) {
        run(QStringLiteral("volume ") + stream.toString(),
            { QStringLiteral("set-sink-input-volume"), stream.toString(), percent(volume) });
    }
    emit changed();
}

void Mixer::stepAppVolume(const QString& key, int direction)
{
    if (QVariantMap* app = findApp(key)) {
        setAppVolume(key, VolumeSteps::step(app->value(QStringLiteral("volume")).toDouble(), direction, VolumeSteps::evenGrid));
        if (app->value(QStringLiteral("muted")).toBool()) {
            setAppMuted(key, false); // like the system volume
        }
    }
}

void Mixer::setAppMuted(const QString& key, bool muted)
{
    QVariantMap* app = findApp(key);
    if (!app) {
        return;
    }
    app->insert(QStringLiteral("muted"), muted);
    for (const QVariant& stream : app->value(QStringLiteral("streams")).toList()) {
        run(QStringLiteral("mute ") + stream.toString(),
            { QStringLiteral("set-sink-input-mute"), stream.toString(), muted ? QStringLiteral("1") : QStringLiteral("0") });
    }
    emit changed();
}

void Mixer::setAppOutput(const QString& key, const QString& output)
{
    QVariantMap* app = findApp(key);
    if (!app) {
        return;
    }
    app->insert(QStringLiteral("output"), output);
    for (const QVariant& o : std::as_const(m_outputs)) {
        if (o.toMap().value(QStringLiteral("name")).toString() == output) {
            app->insert(QStringLiteral("outputDescription"), o.toMap().value(QStringLiteral("description")));
        }
    }
    // To @DEFAULT_SINK@, PipeWire's pulse server sets the target to -1: the
    // stream follows the default output again, and WirePlumber forgets the
    // app's choice.
    for (const QVariant& stream : app->value(QStringLiteral("streams")).toList()) {
        run(QStringLiteral("move ") + stream.toString(),
            { QStringLiteral("move-sink-input"), stream.toString(), output.isEmpty() ? QStringLiteral("@DEFAULT_SINK@") : output });
    }
    emit changed();
}

bool Mixer::stepTaskVolume(const QString& desktopId, int direction)
{
    if (desktopId.isEmpty() || direction == 0) {
        return false;
    }
    // Closed, the page doesn't follow the system: read it again, unless
    // this wheel's own changes are still on their way.
    if (!m_active && !busy() && (!m_loaded.isValid() || m_loaded.elapsed() > 2000)) {
        load();
    }
    for (const QVariantMap& app : std::as_const(m_apps)) {
        if (app.value(QStringLiteral("desktopId")).toString() == desktopId) {
            const QString key = app.value(QStringLiteral("key")).toString();
            stepAppVolume(key, direction);
            emit appOsdRequested(key);
            return true;
        }
    }
    return false;
}

void Mixer::run(const QString& key, const QStringList& arguments)
{
    for (auto& waiting : m_queue) {
        if (waiting.first == key) {
            waiting.second = arguments;
            return;
        }
    }
    m_queue.append({ key, arguments });
    if (m_runner.state() == QProcess::NotRunning) {
        runNext();
    }
}

void Mixer::runNext()
{
    if (m_queue.isEmpty()) {
        m_debounce.start(); // what the system really did
        return;
    }
    const QStringList arguments = m_queue.takeFirst().second;
    m_runner.start(QStringLiteral("pactl"), arguments);
}
