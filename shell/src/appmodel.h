#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
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

    struct Entry {
        QString id; // es. org.kde.dolphin.desktop
        QString name;
        QString genericName;
        QString comment;
        QString keywords;
        QString icon;
        QString exec;
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
    // Il file .desktop di una finestra aperta, a partire dal suo app_id
    // (es. "org.kde.konsole" -> "org.kde.konsole.desktop"). Vuoto se ignoto.
    QString findDesktopId(const QString& appId) const;

signals:
    void queryChanged();
    void countChanged();

private:
    void applyFilter();
    bool launchEntry(const Entry& entry) const;

    QList<Entry> m_all; // ordinate per nome
    QList<int> m_visible; // indici in m_all che corrispondono alla ricerca
    QString m_query;
};
