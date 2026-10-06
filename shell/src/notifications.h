// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Il server delle notifiche del desktop (org.freedesktop.Notifications): le
// app mandano i messaggi qui, la shell li mostra in basso a destra come
// Windows 11. Per il QML è un modello: una riga per notifica visibile.

#include <QAbstractListModel>
#include <QDateTime>
#include <QDBusContext>
#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>
#include <vector>

// Il centro notifiche (Win+N): le notifiche passate, dalla più recente,
// finché non le si chiude. Ci finiscono quelle scadute dal popup (non le
// "transient") e, con "Non disturbare", tutte quelle non critiche.
class NotificationHistory : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    using QAbstractListModel::QAbstractListModel;
    int count() const { return int(m_items.size()); }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class NotificationServer;
    struct Item {
        uint id;
        QString appName;
        QString icon;
        QString summary;
        QString body;
        QVariantList actions;
        bool hasDefault;
        QDateTime time;
    };
    std::vector<Item> m_items; // la più recente in cima
};

class NotificationServer : public QAbstractListModel, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QObject* history READ history CONSTANT)
    // "Non disturbare": niente popup (tranne le critiche), tutto nel centro.
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY doNotDisturbChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        IconRole, // sorgente per Image: tema, file o immagine della notifica
        SummaryRole,
        BodyRole,
        ActionsRole, // [{ key, label }], senza l'azione "default"
        HasDefaultActionRole,
        CriticalRole,
    };

    explicit NotificationServer(QObject* parent = nullptr);
    ~NotificationServer() override;

    // Si prende il nome sul bus di sessione. false se c'è già un altro
    // server (Plasma, quando Vela gira annidato o da una console).
    bool registerService();

    int count() const { return int(m_items.size()); }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Dal QML.
    Q_INVOKABLE void invoke(uint id, const QString& action); // "default" per il clic
    Q_INVOKABLE void dismiss(uint id);
    // Il mouse è sopra le notifiche: non scadono finché non se ne va.
    Q_INVOKABLE void setHovered(bool hovered);
    // Si apre il centro notifiche: i popup ancora a schermo ci entrano,
    // come su Windows (prima restavano sopra, a coprire il calendario).
    Q_INVOKABLE void collectPopups();

    // Il centro notifiche.
    QObject* history() { return &m_history; }
    bool doNotDisturb() const { return m_doNotDisturb; }
    void setDoNotDisturb(bool on);
    Q_INVOKABLE void invokeFromHistory(uint id, const QString& action);
    Q_INVOKABLE void dismissFromHistory(uint id);
    Q_INVOKABLE void clearHistory();

    QImage image(uint id) const { return m_images.value(id); }

    // --- D-Bus ---
public Q_SLOTS:
    Q_SCRIPTABLE QStringList GetCapabilities();
    Q_SCRIPTABLE uint Notify(const QString& app_name, uint replaces_id, const QString& app_icon,
        const QString& summary, const QString& body, const QStringList& actions, const QVariantMap& hints,
        int expire_timeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE QString GetServerInformation(QString& vendor, QString& version, QString& spec_version);

Q_SIGNALS:
    void countChanged();
    void doNotDisturbChanged();
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString& action_key);

private:
    struct Notification {
        uint id;
        QString appName;
        QString icon;
        QString summary;
        QString body;
        QVariantList actions;
        bool hasDefault;
        bool critical;
        bool transient; // non va nel centro notifiche
        std::unique_ptr<QTimer> timer; // scadenza; null: resta finché non la si chiude
    };
    void toHistory(const Notification& n);
    int historyRow(uint id) const;
    enum class Reason : uint { Expired = 1, Dismissed = 2, Closed = 3 };

    void close(uint id, Reason reason);
    int rowOf(uint id) const;
    QString iconFor(uint id, const QString& appIcon, const QVariantMap& hints);

    std::vector<Notification> m_items; // la più recente in fondo
    QHash<uint, QImage> m_images;
    uint m_nextId = 1;
    bool m_hovered = false;
    bool m_doNotDisturb = false;
    NotificationHistory m_history;
};

// "image://notification/<id>": l'immagine allegata a una notifica.
class NotificationImageProvider : public QQuickImageProvider {
public:
    explicit NotificationImageProvider(NotificationServer* server)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_server(server)
    {
    }
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    NotificationServer* m_server;
};
