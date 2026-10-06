// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QCollator>
#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>

#include <atomic>
#include <memory>

// Il contenuto di una cartella per la vista di Esplora: si legge in un
// altro thread (una cartella con migliaia di file non ferma la finestra) e
// arriva a blocchi; poi si ordina, si filtra, si seleziona qui. Con
// `search` diventa la ricerca nelle sottocartelle (i risultati arrivano man
// mano). La cartella si osserva: i cambiamenti fatti da altri compaiono da
// soli.
class FolderModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY pathChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY viewChanged)
    // 0 nome, 1 data di modifica, 2 tipo, 3 dimensione
    Q_PROPERTY(int sortColumn READ sortColumn NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY viewChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionSize READ selectionSize NOTIFY selectionChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    // Cresce a ogni cambio di selezione: per i binding che devono rileggerla.
    Q_PROPERTY(int selectionVersion READ selectionVersion NOTIFY selectionChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        UrlRole,
        IsDirRole,
        IsLinkRole,
        HiddenRole,
        SizeRole,
        SizeTextRole,
        ModifiedRole,
        ModifiedTextRole,
        TypeRole,
        IconRole,
        ThumbnailRole, // un'immagine o un video: c'è una miniatura
        SelectedRole,
        LocationRole, // la cartella che lo contiene (risultati della ricerca)
        CutRole, // tagliato (negli appunti): si mostra sbiadito
    };

    struct Entry {
        QString name;
        QString path;
        bool isDir = false;
        bool isLink = false;
        bool hidden = false;
        qint64 size = 0;
        QDateTime modified;
        QString type;
        QString icon;
        bool thumbnail = false;
    };

    explicit FolderModel(QObject* parent = nullptr);
    ~FolderModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString& path);
    QString search() const { return m_search; }
    void setSearch(const QString& search);
    bool loading() const { return m_loading; }
    bool exists() const { return m_exists; }
    int count() const { return int(m_items.size()); }
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool on);
    int sortColumn() const { return m_sortColumn; }
    bool sortDescending() const { return m_sortDescending; }
    Q_INVOKABLE void sortBy(int column, bool descending);

    Q_INVOKABLE void reload();
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE bool isDirAt(int row) const;
    Q_INVOKABLE int indexOf(const QString& path) const;
    // La prima voce dopo `from` il cui nome comincia per `prefix` (digitando nella vista).
    Q_INVOKABLE int find(const QString& prefix, int from) const;

    // --- selezione ---
    int selectionCount() const { return int(m_selected.size()); }
    qint64 selectionSize() const;
    int currentIndex() const { return m_current; }
    int selectionVersion() const { return m_selectionVersion; }
    void setCurrentIndex(int row);
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE void select(int row); // solo questa
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void selectRange(int from, int to, bool add);
    Q_INVOKABLE void selectRows(const QList<int>& rows, bool add);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void selectPath(const QString& path); // appena compare (nuovo, incollato)
    Q_INVOKABLE QStringList selectedPaths() const;
    Q_INVOKABLE QStringList selectedUrls() const;
    Q_INVOKABLE QString urlAt(int row) const;

signals:
    void pathChanged();
    void searchChanged();
    void loadingChanged();
    void countChanged();
    void viewChanged();
    void selectionChanged();
    void currentIndexChanged();
    // La voce aspettata (selectPath) è comparsa: la vista ci va e, se
    // chiesto, la fa rinominare.
    void pathAppeared(int row);

private:
    void start(bool refresh);
    void receive(quint64 generation, QList<Entry> entries, bool finished);
    void merge(QList<Entry> fresh); // aggiornamento: confronta e cambia solo ciò che serve
    void insertSorted(const Entry& entry);
    void insertBatch(QList<Entry> entries);
    void removeAt(int row);
    void resort(); // ordine o filtro cambiati: si rifà tutto, tenendo la selezione
    void checkPending(int row);
    void readClipboard(); // i file tagliati si vedono sbiaditi
    bool lessThan(const Entry& a, const Entry& b) const;
    void emitSelection(const QSet<int>& changed);

    QString m_path;
    QString m_search;
    bool m_loading = false;
    bool m_exists = true;
    bool m_showHidden = false;
    int m_sortColumn = 0;
    bool m_sortDescending = false;

    QList<Entry> m_items; // quelle mostrate, nell'ordine
    QList<Entry> m_filtered; // quelle nascoste (file che iniziano con il punto)
    QList<Entry> m_incoming; // un aggiornamento che sta arrivando
    bool m_refreshing = false;
    QSet<int> m_selected; // righe
    int m_current = -1;
    QSet<QString> m_cut;
    int m_selectionVersion = 0;
    QString m_pendingSelect;

    QCollator m_collator;
    std::shared_ptr<std::atomic<quint64>> m_generation;
    QFileSystemWatcher m_watcher;
    QTimer m_refresh;
};

// "1,2 MB", come Windows (unità da 1024, una cifra decimale).
QString formatSize(qint64 bytes);
