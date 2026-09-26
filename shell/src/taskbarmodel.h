#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWindow>

class AppModel;
class ForeignToplevel;
class ForeignToplevelManager;

// I pulsanti della taskbar, come su Windows 11: prima le app fissate (aperte
// o no), poi le altre app aperte, nell'ordine in cui sono state avviate.
// Le finestre della stessa app stanno sotto un solo pulsante.
class TaskbarModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList pinnedIds READ pinnedIds WRITE setPinnedIds NOTIFY pinnedIdsChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        PinnedRole,
        WindowCountRole,
        WindowActiveRole,
    };

    TaskbarModel(AppModel* apps, ForeignToplevelManager* windows, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList pinnedIds() const { return m_pinnedIds; }
    void setPinnedIds(const QStringList& ids);

    // Clic sul pulsante: avvia l'app, oppure porta davanti la sua finestra,
    // oppure (se è già quella attiva) la riduce a icona.
    Q_INVOKABLE void activate(int row);
    // Clic centrale: una nuova finestra dell'app.
    Q_INVOKABLE void launchNew(int row);
    // Posizione del pulsante nella finestra della taskbar.
    Q_INVOKABLE void setButtonGeometry(int row, QWindow* panel, const QRectF& rect);

signals:
    void pinnedIdsChanged();

private:
    struct Item {
        QString key; // id del .desktop, oppure "app:<app_id>" se sconosciuto
        QString desktopId;
        QString name;
        QString icon;
        bool pinned = false;
        QList<ForeignToplevel*> windows;
    };

    void rebuild();
    QList<Item> buildItems();
    void sendButtonRects();

    AppModel* m_apps;
    ForeignToplevelManager* m_windows;
    QStringList m_pinnedIds;
    QStringList m_unpinnedOrder; // chiavi delle app non fissate, in ordine di comparsa
    QList<Item> m_items;
    QPointer<QWindow> m_panel;
    QHash<QString, QRect> m_buttonRects; // per chiave dell'app
};
