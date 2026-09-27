#pragma once

// Il server delle notifiche del desktop (org.freedesktop.Notifications): le
// app mandano i messaggi qui, la shell li mostra in basso a destra come
// Windows 11. Per il QML è un modello: una riga per notifica visibile.

#include <QAbstractListModel>
#include <QDBusContext>
#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>
#include <vector>

class NotificationServer : public QAbstractListModel, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

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
        std::unique_ptr<QTimer> timer; // scadenza; null: resta finché non la si chiude
    };
    enum class Reason : uint { Expired = 1, Dismissed = 2, Closed = 3 };

    void close(uint id, Reason reason);
    int rowOf(uint id) const;
    QString iconFor(uint id, const QString& appIcon, const QVariantMap& hints);

    std::vector<Notification> m_items; // la più recente in fondo
    QHash<uint, QImage> m_images;
    uint m_nextId = 1;
    bool m_hovered = false;
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
