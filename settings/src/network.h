#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>

// Rete e Internet, con NetworkManager (nmcli): i collegamenti attivi con i
// loro indirizzi, le reti Wi-Fi intorno, connettersi e disconnettersi.
// I comandi lenti (la ricerca delle reti, la connessione) girano in
// sottofondo: la pagina non si blocca.
class Network : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    // [{device, type ("ethernet"/"wifi"), state, connection, ip, gateway, dns, mac}]
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // [{ssid, signal, secure, active, known}] dalla più forte
    Q_PROPERTY(QVariantList wifiNetworks READ wifiNetworks NOTIFY wifiNetworksChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // L'esito dell'ultima connessione: "" (in corso o niente), "ok", o il messaggio d'errore.
    Q_PROPERTY(QString connectResult READ connectResult NOTIFY connectResultChanged)

public:
    explicit Network(QObject* parent = nullptr);

    bool available() const { return m_available; }
    QVariantList devices() const { return m_devices; }
    QVariantList wifiNetworks() const { return m_wifi; }
    bool scanning() const { return m_scanning; }
    QString connectResult() const { return m_connectResult; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void scan();
    // Password vuota per le reti aperte o già note.
    Q_INVOKABLE void connectWifi(const QString& ssid, const QString& password);
    Q_INVOKABLE void disconnectDevice(const QString& device);
    Q_INVOKABLE void forget(const QString& connection);

signals:
    void devicesChanged();
    void wifiNetworksChanged();
    void scanningChanged();
    void connectResultChanged();

private:
    void readWifi(const QByteArray& output);

    bool m_available = false;
    QVariantList m_devices;
    QVariantList m_wifi;
    QStringList m_known; // nomi delle connessioni salvate
    bool m_scanning = false;
    QString m_connectResult;
};
