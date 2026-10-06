// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>

class AppModel;

// App > App predefinite, come Windows: per ogni uso (browser, posta,
// musica, video, foto, PDF, testo, cartelle) l'app che lo apre, scelta tra
// quelle che lo sanno fare. Si scrive in mimeapps.list con xdg-mime, così
// vale per tutto il sistema (KDE, GNOME, Firefox...).
class DefaultApps : public QObject {
    Q_OBJECT
    // [{key, label, icon, current: {id, name, icon} o vuoto}]
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)

public:
    explicit DefaultApps(AppModel* apps, QObject* parent = nullptr);

    QVariantList categories() const;
    // Le app che sanno aprire quella categoria: [{id, name, icon}].
    Q_INVOKABLE QVariantList candidates(const QString& key) const;
    Q_INVOKABLE void setDefault(const QString& key, const QString& appId);

signals:
    void changed();

private:
    AppModel* m_apps;
};
