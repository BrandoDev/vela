// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantMap>

// Lo stato del sistema per le icone della taskbar e le impostazioni rapide
// (Win+A), come Windows 11: volume (PipeWire con wpctl), rete
// (NetworkManager), Bluetooth (BlueZ), batteria (UPower), profilo di
// risparmio energetico (power-profiles-daemon), luminosità dello schermo
// del portatile (logind). Tutto si aggiorna da solo quando cambia; ciò che
// manca sul sistema risulta "non disponibile" e il pannello non lo mostra.
class SystemStatus : public QObject {
    Q_OBJECT
    // Volume
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable CONSTANT)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    // Rete
    Q_PROPERTY(bool networkConnected READ networkConnected NOTIFY networkChanged)
    Q_PROPERTY(bool networkWireless READ networkWireless NOTIFY networkChanged)
    Q_PROPERTY(QString networkName READ networkName NOTIFY networkChanged)
    Q_PROPERTY(int wifiStrength READ wifiStrength NOTIFY networkChanged)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY networkChanged)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY networkChanged)
    // Bluetooth
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY bluetoothChanged)
    Q_PROPERTY(bool bluetoothEnabled READ bluetoothEnabled NOTIFY bluetoothChanged)
    // Batteria
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY batteryChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY batteryChanged)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY batteryChanged)
    // Risparmio energia
    Q_PROPERTY(bool powerSaverAvailable READ powerSaverAvailable NOTIFY powerProfileChanged)
    Q_PROPERTY(bool powerSaver READ powerSaver NOTIFY powerProfileChanged)
    // "power-saver", "balanced" o "performance" (se il sistema lo offre)
    Q_PROPERTY(QString powerProfile READ powerProfile WRITE setPowerProfile NOTIFY powerProfileChanged)
    Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY powerProfileChanged)
    // Le icone del tema per rete e volume, e la modalità aereo.
    Q_PROPERTY(QString networkIconName READ networkIcon NOTIFY networkChanged)
    Q_PROPERTY(QString volumeIconName READ volumeIcon NOTIFY volumeChanged)
    Q_PROPERTY(bool airplane READ airplaneMode NOTIFY airplaneChanged)
    // Luminosità (solo schermi con retroilluminazione: portatili)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable CONSTANT)
    Q_PROPERTY(double brightness READ brightness NOTIFY brightnessChanged)

public:
    explicit SystemStatus(QObject* parent = nullptr);
    ~SystemStatus() override;

    bool volumeAvailable() const { return m_volumeAvailable; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    Q_INVOKABLE void setVolume(double value);
    Q_INVOKABLE void setMuted(bool muted);

    bool networkConnected() const { return m_networkConnected; }
    bool networkWireless() const { return m_networkWireless; }
    QString networkName() const { return m_networkName; }
    int wifiStrength() const { return m_wifiStrength; }
    bool wifiAvailable() const { return m_wifiAvailable; }
    bool wifiEnabled() const { return m_wifiEnabled; }
    Q_INVOKABLE void setWifiEnabled(bool on);

    bool bluetoothAvailable() const { return !m_adapter.isEmpty(); }
    bool bluetoothEnabled() const { return m_bluetoothEnabled; }
    Q_INVOKABLE void setBluetoothEnabled(bool on);

    // Modalità aereo: niente Wi-Fi né Bluetooth.
    Q_INVOKABLE bool airplaneMode() const;
    Q_INVOKABLE void setAirplaneMode(bool on);

    bool batteryPresent() const { return m_batteryPresent; }
    int batteryPercent() const { return m_batteryPercent; }
    bool batteryCharging() const { return m_batteryCharging; }

    bool powerSaverAvailable() const { return m_powerProfilesAvailable; }
    bool powerSaver() const { return m_powerProfile == QLatin1String("power-saver"); }
    Q_INVOKABLE void setPowerSaver(bool on);
    QString powerProfile() const { return m_powerProfile; }
    void setPowerProfile(const QString& profile);
    QStringList powerProfiles() const { return m_powerProfiles; }

    bool brightnessAvailable() const { return !m_backlight.isEmpty(); }
    double brightness() const { return m_brightness; }
    Q_INVOKABLE void setBrightness(double value);

    // Il nome dell'icona del tema per rete e volume, come le icone di sistema di Windows.
    Q_INVOKABLE QString networkIcon() const;
    Q_INVOKABLE QString volumeIcon() const;

signals:
    void volumeChanged();
    void networkChanged();
    void bluetoothChanged();
    void batteryChanged();
    void powerProfileChanged();
    void brightnessChanged();
    void airplaneChanged();

private Q_SLOTS:
    void refreshNetwork();
    void refreshBluetooth();
    void refreshBattery();
    void refreshPowerProfile();
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated);

private:
    void refreshVolume();
    void readBacklight();

    bool m_volumeAvailable = false;
    double m_volume = 0.0;
    bool m_muted = false;
    QProcess m_subscribe; // pactl subscribe: i cambiamenti dell'audio
    QTimer m_volumeTimer;

    bool m_networkConnected = false;
    bool m_networkWireless = false;
    QString m_networkName;
    int m_wifiStrength = 0;
    bool m_wifiAvailable = false;
    bool m_wifiEnabled = false;

    QString m_adapter; // percorso dell'adattatore BlueZ
    bool m_bluetoothEnabled = false;

    bool m_batteryPresent = false;
    int m_batteryPercent = 0;
    bool m_batteryCharging = false;

    bool m_powerProfilesAvailable = false;
    QString m_powerProfile;
    QStringList m_powerProfiles;

    QString m_backlight; // nome in /sys/class/backlight
    double m_brightness = 0.0;
    int m_maxBrightness = 0;
};
