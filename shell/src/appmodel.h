// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// Elenco delle applicazioni installate, letto dai file .desktop standard
// (gli stessi che usano KDE, GNOME e il resto del mondo Linux).
class AppModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        CommentRole,
    };

    // Un'azione dichiarata dall'app ([Desktop Action ...]): "Nuova finestra"...
    struct Action {
        QString id;
        QString name;
        QString icon;
        QString exec; // con i field code
    };

    struct Entry {
        QString id; // es. org.kde.dolphin.desktop
        QString path; // il file .desktop
        QString rawExec; // con i field code (%f, %u...)
        QList<Action> actions;
        QStringList mimeTypes; // i tipi di file che sa aprire
        QString name;
        QString genericName;
        QString comment;
        QString keywords;
        QString icon;
        QString exec; // senza field code: per la ricerca
        QString program; // il nome del programma (es. "kate"), dal primo argomento di Exec
        QString workDir; // Path=: la cartella in cui avviarla (vuota: la home)
        QString wmClass; // StartupWMClass: l'app_id delle sue finestre, se diverso dall'id
        bool terminal = false;
    };

    explicit AppModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString& query);
    int count() const { return static_cast<int>(m_visible.size()); }

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool launch(int row);
    Q_INVOKABLE bool launchId(const QString& id);
    // Dati di una singola app per id (per le icone fissate sulla taskbar).
    Q_INVOKABLE QVariantMap entry(const QString& id) const;
    // Le azioni dell'app per la jump list: [{id, name, icon}].
    Q_INVOKABLE QVariantList actions(const QString& id) const;
    Q_INVOKABLE bool launchAction(const QString& id, const QString& actionId);
    // Apre un file (o un URL) con quell'app.
    Q_INVOKABLE bool launchWithFile(const QString& id, const QString& url);
    Q_INVOKABLE QString desktopFile(const QString& id) const;
    Q_INVOKABLE QString name(const QString& id) const;
    // Il programma che l'app avvia (es. "kate"), per riconoscerla altrove.
    QString program(const QString& id) const;
    const Entry* find(const QString& id) const;
    // "Apri con": le app che sanno aprire quel tipo di file (e i suoi
    // genitori, es. text/plain per il C++), la predefinita per prima:
    // [{id, name, icon, isDefault}].
    Q_INVOKABLE QVariantList appsForFile(const QString& path) const;
    // Tutte le app, per nome: [{id, name, icon}] ("Scegli un'altra app").
    Q_INVOKABLE QVariantList allApps() const;
    // Un collegamento .desktop fuori dal menu (es. sul desktop): lo avvia.
    Q_INVOKABLE bool launchDesktopFile(const QString& path) const;
    // L'id nel menu di un .desktop che sta altrove (es. steam.desktop sul
    // desktop), se è la stessa app; vuoto altrimenti.
    Q_INVOKABLE QString idForDesktopFile(const QString& path) const;
    const QList<Entry>& all() const { return m_all; }
    // Il file .desktop di una finestra aperta, a partire dal suo app_id
    // (es. "org.kde.konsole" -> "org.kde.konsole.desktop"). Vuoto se ignoto.
    QString findDesktopId(const QString& appId) const;
    // L'icona di una finestra aperta, dal suo app_id.
    Q_INVOKABLE QString iconForAppId(const QString& appId) const;

signals:
    void queryChanged();
    void countChanged();

private:
    void applyFilter();
    // Avvia l'app con i file dati (anche nessuno), secondo Exec (desktopexec.h):
    // il programma con i suoi argomenti, senza passare dalla shell.
    bool launchEntry(const Entry& entry, const QList<QUrl>& files = {}) const;

    QList<Entry> m_all; // ordinate per nome
    QList<int> m_visible; // indici in m_all che corrispondono alla ricerca
    QString m_query;
};

// L'app predefinita per un tipo MIME (id del .desktop), da mimeapps.list.
QString defaultAppFor(const QString& mime);
