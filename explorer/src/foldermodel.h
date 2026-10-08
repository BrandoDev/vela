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

// A folder's content for Explorer's view: read in another thread (a folder
// with thousands of files doesn't stall the window) and delivered in batches;
// then sorted, filtered and selected here. With `search` it becomes a search
// through subfolders (results arrive as they're found). The folder is watched:
// changes made by others show up by themselves.
class FolderModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY pathChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY viewChanged)
    // 0 name, 1 modification date, 2 type, 3 size
    Q_PROPERTY(int sortColumn READ sortColumn NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY viewChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionSize READ selectionSize NOTIFY selectionChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    // Grows on every selection change: for bindings that must read it again.
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
        ThumbnailRole, // an image or a video: there is a thumbnail
        SelectedRole,
        LocationRole, // the folder containing it (search results)
        CutRole, // cut (on the clipboard): shown faded
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
    // The first entry after `from` whose name starts with `prefix` (typing in
    // the view).
    Q_INVOKABLE int find(const QString& prefix, int from) const;

    // --- selection ---
    int selectionCount() const { return int(m_selected.size()); }
    qint64 selectionSize() const;
    int currentIndex() const { return m_current; }
    int selectionVersion() const { return m_selectionVersion; }
    void setCurrentIndex(int row);
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE void select(int row); // only this one
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void selectRange(int from, int to, bool add);
    Q_INVOKABLE void selectRows(const QList<int>& rows, bool add);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void selectPath(const QString& path); // as soon as it appears (new, pasted)
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
    // The awaited entry (selectPath) appeared: the view goes there and, if
    // asked, has it renamed.
    void pathAppeared(int row);

private:
    void start(bool refresh);
    void receive(quint64 generation, QList<Entry> entries, bool finished);
    void merge(QList<Entry> fresh); // update: compares and changes only what's needed
    void insertSorted(const Entry& entry);
    void insertBatch(QList<Entry> entries);
    void removeAt(int row);
    void resort(); // order or filter changed: everything is redone, keeping the selection
    void checkPending(int row);
    void readClipboard(); // cut files are shown faded
    bool lessThan(const Entry& a, const Entry& b) const;
    void emitSelection(const QSet<int>& changed);

    QString m_path;
    QString m_search;
    bool m_loading = false;
    bool m_exists = true;
    bool m_showHidden = false;
    int m_sortColumn = 0;
    bool m_sortDescending = false;

    QList<Entry> m_items; // those shown, in order
    QList<Entry> m_filtered; // those hidden (files starting with a dot)
    QList<Entry> m_incoming; // an update on its way
    bool m_refreshing = false;
    QSet<int> m_selected; // rows
    int m_current = -1;
    QSet<QString> m_cut;
    int m_selectionVersion = 0;
    QString m_pendingSelect;

    QCollator m_collator;
    std::shared_ptr<std::atomic<quint64>> m_generation;
    QFileSystemWatcher m_watcher;
    QTimer m_refresh;
};

// "1.2 MB", like Windows (1024 units, one decimal).
QString formatSize(qint64 bytes);
