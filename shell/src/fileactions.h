#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

// Le finestre dei file della shell, per il desktop e per Esplora:
// - Condividi, come Windows 11: ai dispositivi vicini (i telefoni e i
//   computer associati con KDE Connect), per posta (xdg-email), via
//   Bluetooth;
// - "Scegli un'altra app": aprire con un'app qualsiasi, e farla diventare
//   quella predefinita per quel tipo di file (xdg-mime);
// - Nuovo > Collegamento (links.h).
class FileActions : public QObject {
    Q_OBJECT
    // I dispositivi KDE Connect raggiungibili: [{id, name}].
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY devicesChanged)

public:
    explicit FileActions(QObject* parent = nullptr);

    QVariantList devices() const { return m_devices; }
    bool searching() const { return m_searching; }

    Q_INVOKABLE bool canKdeConnect() const;
    Q_INVOKABLE bool canEmail() const;
    Q_INVOKABLE bool canBluetooth() const;
    Q_INVOKABLE void findDevices();
    Q_INVOKABLE void sendToDevice(const QString& id, const QStringList& paths);
    Q_INVOKABLE void sendByEmail(const QStringList& paths);
    Q_INVOKABLE void sendByBluetooth(const QStringList& paths);

    // Il tipo di file ("Immagine PNG") e la sua estensione (".png").
    Q_INVOKABLE QString typeName(const QString& path) const;
    Q_INVOKABLE QString iconFor(const QString& path) const;
    Q_INVOKABLE QString extension(const QString& path) const;
    // `appId` (un .desktop) diventa l'app predefinita per il tipo di `path`.
    Q_INVOKABLE void setDefaultApp(const QString& appId, const QString& path) const;

    // Nuovo > Collegamento: vuoto se fatto, altrimenti il messaggio d'errore.
    Q_INVOKABLE QString createShortcut(const QString& directory, const QString& target, const QString& name) const;
    // Il nome proposto per il collegamento a `target` dentro `directory`.
    Q_INVOKABLE QString shortcutName(const QString& target, const QString& directory) const;
    // "Crea collegamento" e "Incolla collegamento" sul desktop (o in `directory`).
    Q_INVOKABLE void createLinks(const QStringList& paths, const QString& directory) const;
    Q_INVOKABLE bool pasteLinks(const QString& directory) const;

    // I Preferiti (la sezione della Home di Esplora).
    Q_INVOKABLE bool isFavorite(const QString& path) const;
    Q_INVOKABLE void setFavorite(const QString& path, bool on) const;

signals:
    void devicesChanged();

private:
    QVariantList m_devices;
    bool m_searching = false;
};
