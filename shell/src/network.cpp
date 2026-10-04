#include "network.h"

#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

namespace {

// nmcli -t separa i campi con ':' e scrive "\:" quelli dentro i valori.
QStringList splitTerse(const QString& line)
{
    QStringList fields;
    QString field;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line[i] == u'\\' && i + 1 < line.size()) {
            field += line[++i];
        } else if (line[i] == u':') {
            fields << field;
            field.clear();
        } else {
            field += line[i];
        }
    }
    fields << field;
    return fields;
}

QByteArray nmcli(const QStringList& arguments, int timeout = 3000)
{
    QProcess process;
    process.start(QStringLiteral("nmcli"), arguments);
    process.waitForFinished(timeout);
    return process.readAllStandardOutput();
}

// Il processo che gira in sottofondo, e quando finisce chiama `done`.
template<typename Done>
void runAsync(QObject* owner, const QStringList& arguments, Done done)
{
    auto* process = new QProcess(owner);
    QObject::connect(process, &QProcess::finished, owner, [process, done](int code, QProcess::ExitStatus) {
        done(code, process->readAllStandardOutput(), process->readAllStandardError());
        process->deleteLater();
    });
    process->start(QStringLiteral("nmcli"), arguments);
}

} // namespace

Network::Network(QObject* parent)
    : QObject(parent)
    , m_available(!QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty())
{
    // Si legge quando la pagina si apre (refresh()), non all'avvio.
}

void Network::refresh()
{
    if (!m_available) {
        return;
    }
    QVariantList devices;
    const QStringList lines = QString::fromUtf8(nmcli({ QStringLiteral("-t"), QStringLiteral("-f"),
                                                    QStringLiteral("DEVICE,TYPE,STATE,CONNECTION"), QStringLiteral("device") }))
                                  .split(u'\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList f = splitTerse(line);
        if (f.size() < 4 || (f[1] != QLatin1String("ethernet") && f[1] != QLatin1String("wifi"))) {
            continue;
        }
        QVariantMap device { { QStringLiteral("device"), f[0] }, { QStringLiteral("type"), f[1] },
            { QStringLiteral("state"), f[2] }, { QStringLiteral("connection"), f[3] } };
        // I dettagli: "Proprietà" di Windows.
        const QStringList details = QString::fromUtf8(nmcli({ QStringLiteral("-t"), QStringLiteral("-f"),
                                                          QStringLiteral("GENERAL.HWADDR,IP4.ADDRESS,IP4.GATEWAY,IP4.DNS,IP6.ADDRESS"),
                                                          QStringLiteral("device"), QStringLiteral("show"), f[0] }))
                                        .split(u'\n', Qt::SkipEmptyParts);
        QStringList dns;
        QStringList ipv6;
        for (const QString& detail : details) {
            const qsizetype colon = detail.indexOf(u':');
            const QString key = detail.left(colon).section(u'[', 0, 0);
            const QString value = detail.mid(colon + 1);
            if (key == QLatin1String("GENERAL.HWADDR")) {
                device[QStringLiteral("mac")] = value;
            } else if (key == QLatin1String("IP4.ADDRESS") && !device.contains(QStringLiteral("ip"))) {
                device[QStringLiteral("ip")] = value.section(u'/', 0, 0);
            } else if (key == QLatin1String("IP4.GATEWAY")) {
                device[QStringLiteral("gateway")] = value;
            } else if (key == QLatin1String("IP4.DNS")) {
                dns << value;
            } else if (key == QLatin1String("IP6.ADDRESS")) {
                ipv6 << value.section(u'/', 0, 0);
            }
        }
        device[QStringLiteral("dns")] = dns.join(QStringLiteral(", "));
        device[QStringLiteral("ipv6")] = ipv6.join(QStringLiteral(", "));
        devices.append(device);
    }
    m_devices = devices;
    emit devicesChanged();

    m_known.clear();
    const QStringList connections = QString::fromUtf8(nmcli({ QStringLiteral("-t"), QStringLiteral("-f"),
                                                          QStringLiteral("NAME,TYPE"), QStringLiteral("connection") }))
                                        .split(u'\n', Qt::SkipEmptyParts);
    for (const QString& line : connections) {
        const QStringList f = splitTerse(line);
        if (f.size() >= 2 && f[1].contains(QLatin1String("wireless"))) {
            m_known << f[0];
        }
    }
    readWifi(nmcli({ QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"),
        QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("--rescan"), QStringLiteral("no") }));
}

void Network::readWifi(const QByteArray& output)
{
    QVariantList networks;
    QStringList seen;
    for (const QString& line : QString::fromUtf8(output).split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList f = splitTerse(line);
        if (f.size() < 4 || f[1].isEmpty()) {
            continue; // le reti nascoste
        }
        const bool active = f[0] == QLatin1String("*");
        // La stessa rete da più antenne: una voce sola (la più forte viene prima).
        const qsizetype index = seen.indexOf(f[1]);
        if (index >= 0) {
            if (active) {
                QVariantMap entry = networks[index].toMap();
                entry[QStringLiteral("active")] = true;
                networks[index] = entry;
            }
            continue;
        }
        seen << f[1];
        networks.append(QVariantMap {
            { QStringLiteral("ssid"), f[1] },
            { QStringLiteral("signal"), f[2].toInt() },
            { QStringLiteral("secure"), !f[3].isEmpty() && f[3] != QLatin1String("--") },
            { QStringLiteral("active"), active },
            { QStringLiteral("known"), m_known.contains(f[1]) },
        });
    }
    // Quella connessa in cima, poi dalla più forte.
    std::stable_sort(networks.begin(), networks.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        if (x[QStringLiteral("active")] != y[QStringLiteral("active")]) {
            return x[QStringLiteral("active")].toBool();
        }
        return x[QStringLiteral("signal")].toInt() > y[QStringLiteral("signal")].toInt();
    });
    m_wifi = networks;
    emit wifiNetworksChanged();
}

void Network::scan()
{
    if (!m_available || m_scanning) {
        return;
    }
    m_scanning = true;
    emit scanningChanged();
    runAsync(this,
        { QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"), QStringLiteral("device"),
            QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("--rescan"), QStringLiteral("yes") },
        [this](int, const QByteArray& out, const QByteArray&) {
            readWifi(out);
            m_scanning = false;
            emit scanningChanged();
        });
}

void Network::connectWifi(const QString& ssid, const QString& password)
{
    m_connectResult.clear();
    emit connectResultChanged();
    QStringList arguments { QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("connect"), ssid };
    if (!password.isEmpty()) {
        arguments << QStringLiteral("password") << password;
    }
    runAsync(this, arguments, [this](int code, const QByteArray&, const QByteArray& error) {
        m_connectResult = code == 0 ? QStringLiteral("ok")
                                    : QString::fromUtf8(error).trimmed().section(u'\n', 0, 0).remove(QStringLiteral("Error: "));
        if (m_connectResult.isEmpty()) {
            m_connectResult = QStringLiteral("Impossibile connettersi a questa rete");
        }
        emit connectResultChanged();
        refresh();
    });
}

void Network::disconnectDevice(const QString& device)
{
    runAsync(this, { QStringLiteral("device"), QStringLiteral("disconnect"), device },
        [this](int, const QByteArray&, const QByteArray&) { refresh(); });
}

void Network::forget(const QString& connection)
{
    runAsync(this, { QStringLiteral("connection"), QStringLiteral("delete"), QStringLiteral("id"), connection },
        [this](int, const QByteArray&, const QByteArray&) { refresh(); });
}
