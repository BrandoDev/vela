#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// I desktop virtuali, come li vede la shell: li decide il compositor
// (compositor/src/workspaces.cpp), che a ogni cambiamento manda lo stato in
// JSON; da qui partono i comandi (passa a, nuovo, chiudi, rinomina, sposta
// una finestra...). Li usano la Visualizzazione attività e la taskbar.
class Workspaces : public QObject {
    Q_OBJECT
    Q_PROPERTY(int current READ current NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    // identificativo ext della finestra -> desktop (-1: su tutti)
    Q_PROPERTY(QVariantMap windows READ windows NOTIFY changed)
    Q_PROPERTY(QStringList stickyApps READ stickyApps NOTIFY changed)
    // I gruppi di snap: [{output, windows: [{id, tile: [x0, y0, x1, y1]}]}],
    // tile in dodicesimi dell'area utile.
    Q_PROPERTY(QVariantList snapGroups READ snapGroups NOTIFY changed)

public:
    explicit Workspaces(QObject* parent = nullptr);

    int current() const { return m_current; }
    int count() const { return int(m_names.size()); }
    QStringList names() const { return m_names; }
    QVariantMap windows() const { return m_windows; }
    QStringList stickyApps() const { return m_stickyApps; }
    QVariantList snapGroups() const { return m_snapGroups; }

    // Il desktop di una finestra (-1: tutti; -2: sconosciuta).
    Q_INVOKABLE int workspaceOf(const QString& window) const;

    Q_INVOKABLE void switchTo(int index);
    Q_INVOKABLE void create(bool switchTo = false);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void rename(int index, const QString& name);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void moveWindow(const QString& window, int index);
    Q_INVOKABLE void moveWindowToNew(const QString& window);
    Q_INVOKABLE void setWindowSticky(const QString& window, bool on);
    Q_INVOKABLE void setAppSticky(const QString& window, bool on);

    // Lo stato mandato dal compositor ("workspaces <json>").
    void update(const QByteArray& json);
    // Lo chiede al compositor (all'avvio della shell).
    void query();

signals:
    void changed();

private:
    QStringList m_names;
    int m_current = 0;
    QVariantMap m_windows;
    QStringList m_stickyApps;
    QVariantList m_snapGroups;
};
