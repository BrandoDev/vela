#pragma once

#include <QObject>
#include <QVariantList>

// I luoghi di Esplora, come Windows 11: Accesso rapido (le cartelle
// dell'utente e quelle aggiunte, ricordate), le unità di "Questo PC" con lo
// spazio libero, i file recenti (recently-used.xbel, lo stesso di KDE e
// GNOME) e il Cestino.
class Places : public QObject {
    Q_OBJECT
    // [{name, path, icon, pinned}]
    Q_PROPERTY(QVariantList quickAccess READ quickAccess NOTIFY quickAccessChanged)
    // [{name, path, icon, total, free, device, removable}]
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
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
    Q_INVOKABLE bool isPinned(const QString& path) const;
    Q_INVOKABLE void pin(const QString& path);
    Q_INVOKABLE void unpin(const QString& path);
    // I file usati di recente: [{name, path, icon, modified, location}], dal più recente.
    Q_INVOKABLE QVariantList recentFiles(int limit) const;
    // Il nome da mostrare per una cartella ("Documenti", "Disco locale"...).
    Q_INVOKABLE QString displayName(const QString& path) const;
    Q_INVOKABLE QString iconFor(const QString& path) const;

signals:
    void quickAccessChanged();
    void drivesChanged();

private:
    void loadQuickAccess();
    void saveQuickAccess();

    QStringList m_pinned;
    QVariantList m_quickAccess;
    QVariantList m_drives;
};
