#pragma once

#include <QHash>
#include <QObject>
#include <QVariantMap>
#include <QVariantList>

// I luoghi di Esplora, come Windows 11: Accesso rapido (le cartelle
// dell'utente e quelle aggiunte, ricordate), i Preferiti (file), le unità
// di "Questo PC" con lo spazio libero, anche quelle non ancora montate (da
// udisks: si montano aprendole), i file recenti (recently-used.xbel, lo
// stesso di KDE e GNOME) e il Cestino.
class Places : public QObject {
    Q_OBJECT
    // [{name, path, icon, pinned}]
    Q_PROPERTY(QVariantList quickAccess READ quickAccess NOTIFY quickAccessChanged)
    // [{name, path, icon, total, free, device, removable, mounted, volume}]:
    // le non montate hanno path vuoto e `volume` (l'oggetto udisks).
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    // [{name, path, icon, modified, location}]
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged)
    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString trash READ trash CONSTANT) // la cartella dei file nel Cestino
    Q_PROPERTY(QString userName READ userName CONSTANT)

public:
    explicit Places(QObject* parent = nullptr);

    QVariantList quickAccess() const { return m_quickAccess; }
    QVariantList drives() const { return m_drives; }
    QString home() const;
    QString trash() const;
    QString userName() const;

    Q_INVOKABLE void refreshDrives();
    // Monta un'unità (udisks, con la password se serve): poi `mounted`.
    Q_INVOKABLE void mount(const QString& volume);
    // Espelli: smonta e, se si può, spegne il dispositivo (chiavette, dischi USB).
    Q_INVOKABLE void eject(const QString& device);

    QVariantList favorites() const;
    Q_INVOKABLE bool isFavorite(const QString& path) const;
    Q_INVOKABLE void setFavorite(const QString& path, bool on);
    Q_INVOKABLE bool isPinned(const QString& path) const;
    Q_INVOKABLE void pin(const QString& path);
    Q_INVOKABLE void unpin(const QString& path);
    // I file usati di recente: [{name, path, icon, modified, location}], dal più recente.
    Q_INVOKABLE QVariantList recentFiles(int limit) const;
    // Il nome da mostrare per una cartella ("Documenti", "Disco locale"...).
    Q_INVOKABLE QString displayName(const QString& path) const;
    Q_INVOKABLE QString iconFor(const QString& path) const;

private Q_SLOTS:
    void onVolumesChanged(); // udisks: un disco collegato, scollegato, montato

signals:
    void quickAccessChanged();
    void drivesChanged();
    void favoritesChanged();
    // Montata (dove), o non si è potuto (error non vuoto).
    void mounted(const QString& volume, const QString& path, const QString& error);
    void ejected(const QString& error);

private:
    void loadQuickAccess();
    void saveQuickAccess();
    void readVolumes(); // udisks, in sottofondo
    void publishDrives();

    QStringList m_pinned;
    QVariantList m_quickAccess;
    QVariantList m_drives;
    QVariantList m_mountedDrives; // da /proc/mounts
    QVariantList m_volumes; // da udisks, non montate
    // device (/dev/...) -> {block: oggetto, drive: oggetto del disco, removable}
    QHash<QString, QVariantMap> m_devices;
    bool m_watchingVolumes = false;
    bool m_readingVolumes = false;
};
