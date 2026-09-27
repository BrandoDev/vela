#pragma once

// L'area di notifica (tray): le icone che le app mettono accanto
// all'orologio (Telegram, Discord, Steam...). Il protocollo è
// StatusNotifierItem (D-Bus), con i menu in com.canonical.dbusmenu.
//
// - Il "watcher" è il registro in cui le app annunciano le loro icone. Se
//   nessuno lo offre già (Plasma, quando Vela gira dentro KDE), lo offre
//   la shell.
// - L'"host" è chi mostra le icone: questo modello, per il QML.

#include <QAbstractListModel>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QStringList>

#include <memory>
#include <vector>

class QDBusServiceWatcher;
class QDBusPendingCallWatcher;

// Il registro, sul bus come org.kde.StatusNotifierWatcher.
class StatusNotifierWatcher : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ items)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit StatusNotifierWatcher(QObject* parent = nullptr);
    bool registerService(); // false se c'è già un altro registro

    QStringList items() const { return m_items; }
    bool hostRegistered() const { return true; }
    int protocolVersion() const { return 0; }

public Q_SLOTS:
    Q_SCRIPTABLE void RegisterStatusNotifierItem(const QString& service);
    Q_SCRIPTABLE void RegisterStatusNotifierHost(const QString& service);

Q_SIGNALS:
    Q_SCRIPTABLE void StatusNotifierItemRegistered(const QString& service);
    Q_SCRIPTABLE void StatusNotifierItemUnregistered(const QString& service);
    Q_SCRIPTABLE void StatusNotifierHostRegistered();

private:
    QStringList m_items; // "servizio/percorso"
    QDBusServiceWatcher* m_owners;
};

class TrayModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IconRole = Qt::UserRole + 1,
        TitleRole,
        AttentionRole, // chiede attenzione (es. messaggi non letti)
    };

    explicit TrayModel(QObject* parent = nullptr);
    ~TrayModel() override;

    // Si collega al registro (nostro o di un altro) e carica le icone.
    void start();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Dal QML.
    // anchorX: il centro dell'icona sullo schermo, dove aprire il menu.
    Q_INVOKABLE void activate(int row, int anchorX); // clic sinistro (o menu, se l'icona è solo un menu)
    Q_INVOKABLE void secondaryActivate(int row); // clic centrale
    Q_INVOKABLE void scroll(int row, int delta);
    // Chiede il menu dell'icona: arriva con menuReady.
    Q_INVOKABLE void requestMenu(int row, int anchorX);
    // Una voce del menu mostrato è stata scelta.
    Q_INVOKABLE void activateMenuEntry(int row, int entryId);

    QImage pixmap(const QString& key) const { return m_pixmaps.value(key); }

Q_SIGNALS:
    // entries: [{ id, label, enabled, separator, checkable, checked, radio, icon, children }]
    void menuReady(int row, int anchorX, const QVariantList& entries);

private:
    struct Item;
    void addItem(const QString& address);
    void removeItem(const QString& address);
    void refresh(Item* item);
    int rowOf(const Item* item) const;
    Item* itemAt(int row) const;

    std::vector<std::unique_ptr<Item>> m_items;
    QHash<QString, QImage> m_pixmaps; // chiave: indirizzo/seriale
    StatusNotifierWatcher* m_watcher = nullptr; // se il registro è nostro
    uint m_serial = 0;

private Q_SLOTS:
    // Dal registro di un altro (Plasma), via D-Bus.
    void onItemRegistered(const QString& address) { addItem(address); }
    void onItemUnregistered(const QString& address) { removeItem(address); }
};

// "image://tray/<chiave>": le icone disegnate dalle app (IconPixmap).
class TrayImageProvider : public QQuickImageProvider {
public:
    explicit TrayImageProvider(TrayModel* tray)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_tray(tray)
    {
    }
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    TrayModel* m_tray;
};
