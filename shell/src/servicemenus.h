// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// KDE "service menus" (kio/servicemenus/*.desktop): the entries apps add to
// the file menu (such as Filelight's "Show disk usage statistics"). In Vela
// they're under "Show more options", like classic extensions in the Windows 11
// menu (docs/renderer.md §14.9).
class ServiceMenus : public QObject {
    Q_OBJECT

public:
    explicit ServiceMenus(QObject* parent = nullptr);

    // The entries that apply to these files: [{id, text, icon, submenu}].
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
