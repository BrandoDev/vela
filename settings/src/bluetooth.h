#pragma once

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// Bluetooth e dispositivi, con BlueZ via D-Bus: i dispositivi associati
// (connessi o no, con la batteria se la dicono), la ricerca di quelli nuovi
// ("Aggiungi dispositivo"), associare, connettere, disconnettere, rimuovere.
class Bluetooth : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered WRITE setPowered NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(QString adapterName READ adapterName NOTIFY changed)
    // [{path, address, name, icon, paired, connected, battery (-1 se non nota), busy}]
    Q_PROPERTY(QVariantList paired READ paired NOTIFY changed)
    Q_PROPERTY(QVariantList found READ found NOTIFY changed) // trovati dalla ricerca, non associati
    // L'ultimo errore da mostrare ("" se nessuno).
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit Bluetooth(QObject* parent = nullptr);

    bool available() const { return !m_adapter.isEmpty(); }
    bool powered() const { return m_powered; }
    void setPowered(bool on);
    bool discovering() const { return m_discovering; }
    QString adapterName() const { return m_adapterName; }
    QVariantList paired() const;
    QVariantList found() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    // Associa (se serve), si fida e connette: "Aggiungi dispositivo".
    Q_INVOKABLE void pairAndConnect(const QString& path);
    Q_INVOKABLE void connectDevice(const QString& path);
    Q_INVOKABLE void disconnectDevice(const QString& path);
    Q_INVOKABLE void removeDevice(const QString& path);

signals:
    void changed();
    void errorChanged();

private Q_SLOTS:
    void onInterfacesAdded(const QDBusObjectPath& path, const QMap<QString, QVariantMap>& interfaces);
    void onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated);

private:
    void callDevice(const QString& path, const QString& method, std::function<void()> then = {});
    void setBusy(const QString& path, bool busy);
    void fail(const QString& message);
    QVariantList devices(bool paired) const;

    QString m_adapter;
    QString m_adapterName;
    bool m_powered = false;
    bool m_discovering = false;
    QMap<QString, QVariantMap> m_devices; // percorso -> proprietà di Device1 (+ "Battery", "busy")
    QString m_error;
    bool m_watching = false;
};
