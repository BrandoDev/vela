// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>
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
    // Le app fissate sono già state salvate (altrimenti si parte da quelle predefinite).
    Q_PROPERTY(bool pinsSaved READ pinsSaved CONSTANT)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        PinnedRole,
        WindowCountRole,
        WindowActiveRole,
        DesktopIdRole,
    };

    TaskbarModel(AppModel* apps, ForeignToplevelManager* windows, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList pinnedIds() const { return m_pinnedIds; }
    void setPinnedIds(const QStringList& ids);
    bool pinsSaved() const { return m_pinsSaved; }
    Q_INVOKABLE bool isPinned(const QString& desktopId) const { return m_pinnedIds.contains(desktopId); }
    Q_INVOKABLE void pin(const QString& desktopId);
    Q_INVOKABLE void unpin(const QString& desktopId);

    // Clic sul pulsante: avvia l'app, oppure porta davanti la sua finestra,
    // oppure (se è già quella attiva) la riduce a icona.
    Q_INVOKABLE void activate(int row);
    // Il pulsante trascinato in un altro posto: le app fissate restano
    // davanti a quelle solo aperte, e il loro ordine si ricorda.
    Q_INVOKABLE void move(int from, int to);
    // Clic centrale: una nuova finestra dell'app.
    Q_INVOKABLE void launchNew(int row);
    // "Chiudi finestra" / "Chiudi tutte le finestre".
    Q_INVOKABLE void closeWindows(int row);
    // "Termina attività": il compositor chiude i processi delle sue finestre.
    Q_INVOKABLE void endTask(int row);
    // Il menu della finestra dal pulsante (Maiusc+clic destro), sulla sua
    // finestra più recente: {maximized, minimized}; e le sue azioni
    // (restore, move, resize, minimize, maximize, close).
    Q_INVOKABLE QVariantMap windowState(int row) const;
    Q_INVOKABLE void windowAction(int row, const QString& action);
    // Win+D e "Desktop" nel menu Win+X: riduce tutto a icona, e la volta
    // dopo rimette com'era.
    Q_INVOKABLE void toggleDesktop();
    // Gli app_id delle sue finestre aperte (per trovarle nelle anteprime).
    Q_INVOKABLE QStringList appIds(int row) const;
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
    ForeignToplevel* recentWindow(int row) const;
    void sendButtonRects();

    AppModel* m_apps;
    ForeignToplevelManager* m_windows;
    QStringList m_pinnedIds;
    QStringList m_unpinnedOrder; // chiavi delle app non fissate, in ordine di comparsa
    QList<Item> m_items;
    QPointer<QWindow> m_panel;
    QHash<QString, QRect> m_buttonRects; // per chiave dell'app
    bool m_pinsSaved = false;
    QList<QPointer<ForeignToplevel>> m_hiddenByDesktop; // ridotte a icona da "Mostra desktop"
};
