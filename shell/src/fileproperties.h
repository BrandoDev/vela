#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>

// I dati della finestra Proprietà (Alt+Invio sul desktop), come in Windows:
// tipo, percorso, dimensioni, date, attributi, autorizzazioni. La
// dimensione delle cartelle si conta in un thread e arriva dopo
// (sizeCounted), così la finestra si apre subito.
class FileProperties : public QObject {
    Q_OBJECT

public:
    explicit FileProperties(QObject* parent = nullptr);

    // Per uno o più file. Restituisce anche "token": lo stesso di
    // sizeCounted, per non mescolare conteggi di finestre diverse.
    Q_INVOKABLE QVariantMap describe(const QStringList& paths);

    // Attributo "Sola lettura": toglie (o ridà al proprietario) la scrittura.
    Q_INVOKABLE bool setReadOnly(const QStringList& paths, bool readOnly);
    // Autorizzazioni: 9 bit come chmod (proprietario, gruppo, altri × rwx).
    Q_INVOKABLE bool setPermissions(const QString& path, int bits);
    // "Apri con" → Cambia: l'app predefinita per quel tipo di file.
    Q_INVOKABLE void setDefaultApp(const QString& path, const QString& desktopId);
    // Dimensioni nel formato di Windows: "1,23 MB (1.290.240 byte)".
    Q_INVOKABLE QString formatSize(double bytes) const;

signals:
    void sizeCounted(int token, double bytes, double onDisk, int files, int folders);

private:
    QThreadPool m_pool;
    int m_token = 0;
};
