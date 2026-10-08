// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class AppModel;

// Jump list files, like Windows 11: each app's recent files (from the shared
// registry ~/.local/share/recently-used.xbel, which notes which app opened
// each file) and those the user pinned there.
class JumpLists : public QObject {
    Q_OBJECT

public:
    explicit JumpLists(AppModel* apps, QObject* parent = nullptr);

    // [{url, name, icon}], most recent first. Without pinned and removed ones.
    Q_INVOKABLE QVariantList recent(const QString& desktopId, int limit = 10) const;
    // Pinned files, in the order they were added.
    Q_INVOKABLE QVariantList pinned(const QString& desktopId) const;
    Q_INVOKABLE void setPinned(const QString& desktopId, const QString& url, bool pinned);
    // "Remove from this list": it no longer shows among that app's recent
    // files.
    Q_INVOKABLE void forget(const QString& desktopId, const QString& url);

private:
    QVariantMap describe(const QString& url) const;

    AppModel* m_apps;
};
