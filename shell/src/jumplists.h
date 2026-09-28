#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class AppModel;

// I file delle jump list, come su Windows 11: i recenti di ogni app (dal
// registro condiviso ~/.local/share/recently-used.xbel, che annota quale
// app ha aperto ogni file) e quelli che l'utente vi ha fissato.
class JumpLists : public QObject {
    Q_OBJECT

public:
    explicit JumpLists(AppModel* apps, QObject* parent = nullptr);

    // [{url, name, icon}], dal più recente. Senza i fissati e i tolti.
    Q_INVOKABLE QVariantList recent(const QString& desktopId, int limit = 10) const;
    // I file fissati, nell'ordine in cui sono stati aggiunti.
    Q_INVOKABLE QVariantList pinned(const QString& desktopId) const;
    Q_INVOKABLE void setPinned(const QString& desktopId, const QString& url, bool pinned);
    // "Rimuovi dall'elenco": non compare più tra i recenti di quell'app.
    Q_INVOKABLE void forget(const QString& desktopId, const QString& url);

private:
    QVariantMap describe(const QString& url) const;

    AppModel* m_apps;
};
