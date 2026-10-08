// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemstatus.h"

#include "bluetoothpower.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

#include <csignal>
#include <sys/prctl.h>

namespace {

const QString nm = QStringLiteral("org.freedesktop.NetworkManager");
const QString nmPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

QVariant dbusGet(const QDBusConnection& bus, const QString& service, const QString& path, const QString& interface,
    const QString& name)
{
    QDBusInterface props(service, path, propertiesInterface, bus);
    const QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), interface, name);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

void dbusSet(const QDBusConnection& bus, const QString& service, const QString& path, const QString& interface,
    const QString& name, const QVariant& value)
{
    QDBusInterface props(service, path, propertiesInterface, bus);
    props.asyncCall(QStringLiteral("Set"), interface, name, QVariant::fromValue(QDBusVariant(value)));
}

} // namespace

using ManagedObjects = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
Q_DECLARE_METATYPE(ManagedObjects)

SystemStatus::SystemStatus(QObject* parent)
    : QObject(parent)
{
    QDBusConnection system = QDBusConnection::systemBus();
    connect(this, &SystemStatus::networkChanged, this, &SystemStatus::airplaneChanged);
    connect(this, &SystemStatus::bluetoothChanged, this, &SystemStatus::airplaneChanged);

    // --- Volume: wpctl to read and write, pactl subscribe to know when.
    m_volumeAvailable = !QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty();
    if (m_volumeAvailable) {
        m_volumeTimer.setSingleShot(true);
        m_volumeTimer.setInterval(50); // events come in bursts
        connect(&m_volumeTimer, &QTimer::timeout, this, &SystemStatus::refreshVolume);
        if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty()) {
            connect(&m_subscribe, &QProcess::readyReadStandardOutput, this, [this] {
                const QByteArray lines = m_subscribe.readAllStandardOutput();
                if (lines.contains("sink") || lines.contains("server")) {
                    m_volumeTimer.start();
                }
            });
            // If we exit abruptly (crash), pactl must not be left orphaned.
            m_subscribe.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
            m_subscribe.start(QStringLiteral("pactl"), { QStringLiteral("subscribe") });
        }
        refreshVolume();
    }

    // --- Network: NetworkManager.
    system.connect(nm, nmPath, propertiesInterface, QStringLiteral("PropertiesChanged"), this,
        SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    refreshNetwork();

    // --- Bluetooth: BlueZ's first adapter.
    qDBusRegisterMetaType<ManagedObjects>();
    refreshBluetooth();

    // --- Battery: UPower's "display" device.
    system.connect(QStringLiteral("org.freedesktop.UPower"), QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
        propertiesInterface, QStringLiteral("PropertiesChanged"), this, SLOT(refreshBattery()));
    refreshBattery();

    // --- Power profiles.
    system.connect(QStringLiteral("org.freedesktop.UPower.PowerProfiles"), QStringLiteral("/org/freedesktop/UPower/PowerProfiles"),
        propertiesInterface, QStringLiteral("PropertiesChanged"), this, SLOT(refreshPowerProfile()));
    refreshPowerProfile();

    // --- Brightness: the first backlight (laptops).
    const QStringList backlights = QDir(QStringLiteral("/sys/class/backlight")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (!backlights.isEmpty()) {
        m_backlight = backlights.first();
        readBacklight();
    }
}

SystemStatus::~SystemStatus()
{
    if (m_subscribe.state() != QProcess::NotRunning) {
        m_subscribe.kill();
        m_subscribe.waitForFinished(500);
    }
}

// ----------------------------------------------------------------- volume --

void SystemStatus::refreshVolume()
{
    QProcess process;
    process.start(QStringLiteral("wpctl"), { QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@") });
    if (!process.waitForFinished(500)) {
        return;
    }
    // "Volume: 0.80" or "Volume: 0.80 [MUTED]"
    const QString out = QString::fromUtf8(process.readAllStandardOutput());
    static const QRegularExpression pattern(QStringLiteral("Volume:\\s*([0-9.]+)"));
    const QRegularExpressionMatch match = pattern.match(out);
    if (!match.hasMatch()) {
        return;
    }
    const double volume = match.captured(1).toDouble();
    const bool muted = out.contains(QLatin1String("MUTED"));
    if (!qFuzzyCompare(volume + 1.0, m_volume + 1.0) || muted != m_muted) {
        m_volume = volume;
        m_muted = muted;
        emit volumeChanged();
    }
}

void SystemStatus::setVolume(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    QProcess::startDetached(QStringLiteral("wpctl"),
        { QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QString::number(value, 'f', 2) });
    m_volume = value; // at once, without waiting for the event (the slider doesn't jump)
    emit volumeChanged();
}

void SystemStatus::setMuted(bool muted)
{
    QProcess::startDetached(QStringLiteral("wpctl"),
        { QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), muted ? QStringLiteral("1") : QStringLiteral("0") });
    m_muted = muted;
    emit volumeChanged();
}

QString SystemStatus::volumeIcon() const
{
    if (m_muted || m_volume <= 0.0) {
        return QStringLiteral("audio-volume-muted");
    }
    return m_volume < 0.34 ? QStringLiteral("audio-volume-low")
        : m_volume < 0.67  ? QStringLiteral("audio-volume-medium")
                           : QStringLiteral("audio-volume-high");
}

// ---------------------------------------------------------------- network --

void SystemStatus::onPropertiesChanged(const QString& interface, const QVariantMap&, const QStringList&)
{
    if (interface == nm) {
        refreshNetwork();
    }
}

void SystemStatus::refreshNetwork()
{
    QDBusConnection system = QDBusConnection::systemBus();
    const uint state = dbusGet(system, nm, nmPath, nm, QStringLiteral("State")).toUInt();
    m_networkConnected = state >= 60; // NM_STATE_CONNECTED_SITE and above
    m_wifiEnabled = dbusGet(system, nm, nmPath, nm, QStringLiteral("WirelessEnabled")).toBool();
    m_wifiAvailable = dbusGet(system, nm, nmPath, nm, QStringLiteral("WirelessHardwareEnabled")).toBool();
    m_networkName.clear();
    m_networkWireless = false;
    m_wifiStrength = 0;
    const QDBusObjectPath primary
        = qvariant_cast<QDBusObjectPath>(dbusGet(system, nm, nmPath, nm, QStringLiteral("PrimaryConnection")));
    if (!primary.path().isEmpty() && primary.path() != QLatin1String("/")) {
        const QString active = nm + QStringLiteral(".Connection.Active");
        m_networkName = dbusGet(system, nm, primary.path(), active, QStringLiteral("Id")).toString();
        const QString type = dbusGet(system, nm, primary.path(), active, QStringLiteral("Type")).toString();
        m_networkWireless = type == QLatin1String("802-11-wireless");
        if (m_networkWireless) {
            const QDBusObjectPath ap
                = qvariant_cast<QDBusObjectPath>(dbusGet(system, nm, primary.path(), active, QStringLiteral("SpecificObject")));
            m_wifiStrength = dbusGet(system, nm, ap.path(), nm + QStringLiteral(".AccessPoint"), QStringLiteral("Strength")).toInt();
        }
    }
    emit networkChanged();
}

void SystemStatus::setWifiEnabled(bool on)
{
    dbusSet(QDBusConnection::systemBus(), nm, nmPath, nm, QStringLiteral("WirelessEnabled"), on);
}

QString SystemStatus::networkIcon() const
{
    if (!m_networkConnected) {
        return QStringLiteral("network-offline");
    }
    if (!m_networkWireless) {
        return QStringLiteral("network-wired");
    }
    return m_wifiStrength > 75 ? QStringLiteral("network-wireless-signal-excellent")
        : m_wifiStrength > 50  ? QStringLiteral("network-wireless-signal-good")
        : m_wifiStrength > 25  ? QStringLiteral("network-wireless-signal-ok")
                               : QStringLiteral("network-wireless-signal-weak");
}

// -------------------------------------------------------------- Bluetooth --

void SystemStatus::refreshBluetooth()
{
    QDBusConnection system = QDBusConnection::systemBus();
    if (m_adapter.isEmpty()) {
        QDBusInterface manager(QStringLiteral("org.bluez"), QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"), system);
        const QDBusReply<ManagedObjects> objects = manager.call(QStringLiteral("GetManagedObjects"));
        if (objects.isValid()) {
            for (auto it = objects.value().cbegin(); it != objects.value().cend(); ++it) {
                if (it.value().contains(QStringLiteral("org.bluez.Adapter1"))) {
                    m_adapter = it.key().path();
                    system.connect(QStringLiteral("org.bluez"), m_adapter, propertiesInterface,
                        QStringLiteral("PropertiesChanged"), this, SLOT(refreshBluetooth()));
                    break;
                }
            }
        }
    }
    if (!m_adapter.isEmpty()) {
        m_bluetoothEnabled
            = dbusGet(system, QStringLiteral("org.bluez"), m_adapter, QStringLiteral("org.bluez.Adapter1"), QStringLiteral("Powered")).toBool();
    }
    emit bluetoothChanged();
}

void SystemStatus::setBluetoothEnabled(bool on)
{
    vela::bluetooth::setPowered(m_adapter, on); // even if blocked by rfkill
}

bool SystemStatus::airplaneMode() const
{
    return (!m_wifiAvailable || !m_wifiEnabled) && (m_adapter.isEmpty() || !m_bluetoothEnabled)
        && (m_wifiAvailable || !m_adapter.isEmpty());
}

void SystemStatus::setAirplaneMode(bool on)
{
    if (m_wifiAvailable) {
        setWifiEnabled(!on);
    }
    setBluetoothEnabled(!on);
}

// ----------------------------------------------------------------- battery --

void SystemStatus::refreshBattery()
{
    QDBusConnection system = QDBusConnection::systemBus();
    const QString service = QStringLiteral("org.freedesktop.UPower");
    const QString path = QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
    const QString device = QStringLiteral("org.freedesktop.UPower.Device");
    // Type 2: battery (a desktop's DisplayDevice says "not present").
    m_batteryPresent = dbusGet(system, service, path, device, QStringLiteral("IsPresent")).toBool()
        && dbusGet(system, service, path, device, QStringLiteral("Type")).toUInt() == 2;
    m_batteryPercent = int(std::lround(dbusGet(system, service, path, device, QStringLiteral("Percentage")).toDouble()));
    const uint state = dbusGet(system, service, path, device, QStringLiteral("State")).toUInt();
    m_batteryCharging = state == 1 || state == 4; // charging, charged
    emit batteryChanged();
}

// ---------------------------------------------------------- power profiles --

void SystemStatus::refreshPowerProfile()
{
    const QVariant profile = dbusGet(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
        QStringLiteral("/org/freedesktop/UPower/PowerProfiles"), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
        QStringLiteral("ActiveProfile"));
    m_powerProfilesAvailable = profile.isValid();
    m_powerProfile = profile.toString();
    // The offered profiles: aa{sv}, each with the "Profile" key.
    m_powerProfiles.clear();
    if (m_powerProfilesAvailable) {
        const QVariant profiles = dbusGet(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
            QStringLiteral("/org/freedesktop/UPower/PowerProfiles"), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
            QStringLiteral("Profiles"));
        const QDBusArgument list = profiles.value<QDBusArgument>();
        if (list.currentType() == QDBusArgument::ArrayType) {
            list.beginArray();
            while (!list.atEnd()) {
                QVariantMap entry;
                list >> entry;
                m_powerProfiles << entry.value(QStringLiteral("Profile")).toString();
            }
            list.endArray();
        }
    }
    emit powerProfileChanged();
}

void SystemStatus::setPowerSaver(bool on)
{
    setPowerProfile(on ? QStringLiteral("power-saver") : QStringLiteral("balanced"));
}

void SystemStatus::setPowerProfile(const QString& profile)
{
    dbusSet(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
        QStringLiteral("/org/freedesktop/UPower/PowerProfiles"), QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
        QStringLiteral("ActiveProfile"), profile);
}

// -------------------------------------------------------------- brightness --

void SystemStatus::readBacklight()
{
    const QString base = QStringLiteral("/sys/class/backlight/") + m_backlight;
    auto read = [&](const char* name) {
        QFile file(base + u'/' + QLatin1String(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toInt() : 0;
    };
    m_maxBrightness = read("max_brightness");
    m_brightness = m_maxBrightness > 0 ? double(read("brightness")) / m_maxBrightness : 0.0;
    emit brightnessChanged();
}

void SystemStatus::setBrightness(double value)
{
    if (m_backlight.isEmpty() || m_maxBrightness <= 0) {
        return;
    }
    value = std::clamp(value, 0.01, 1.0); // never entirely off
    // logind lets the session change the backlight, without root.
    QDBusInterface session(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
        QStringLiteral("org.freedesktop.login1.Session"), QDBusConnection::systemBus());
    session.asyncCall(QStringLiteral("SetBrightness"), QStringLiteral("backlight"), m_backlight,
        uint(std::lround(value * m_maxBrightness)));
    m_brightness = value;
    emit brightnessChanged();
}
