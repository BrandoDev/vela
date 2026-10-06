// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// I "service menu" di KDE (kio/servicemenus/*.desktop): le voci che le app
// aggiungono al menu dei file (es. "Visualizza le statistiche di utilizzo
// del disco" di Filelight). In Vela stanno in "Mostra altre opzioni", come
// le estensioni classiche nel menu di Windows 11 (docs/renderer.md §14.9).
class ServiceMenus : public QObject {
    Q_OBJECT

public:
    explicit ServiceMenus(QObject* parent = nullptr);

    // Le voci che valgono per questi file: [{id, text, icon, submenu}].
    Q_INVOKABLE QVariantList actionsFor(const QStringList& paths);
    Q_INVOKABLE void run(int id, const QStringList& paths) const;

private:
    struct Action {
        QString name;
        QString icon;
        QString exec;
        QString submenu;
        QStringList mimeTypes;
        int minUrls = 1;
        int maxUrls = -1;
        QList<int> requiredUrls;
    };
    void load();

    QList<Action> m_actions;
    bool m_loaded = false;
};
