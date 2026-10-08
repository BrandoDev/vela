// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>

// The data of the Properties window (Alt+Enter on the desktop), like Windows:
// type, location, size, dates, attributes, permissions. Folder sizes are
// counted in a thread and arrive later (sizeCounted), so the window opens at
// once.
class FileProperties : public QObject {
    Q_OBJECT

public:
    explicit FileProperties(QObject* parent = nullptr);

    // For one or more files. Also returns "token": the same as sizeCounted's,
    // so counts from different windows don't mix.
    Q_INVOKABLE QVariantMap describe(const QStringList& paths);

    // The "Read-only" attribute: removes (or gives back to the owner) write
    // permission.
    Q_INVOKABLE bool setReadOnly(const QStringList& paths, bool readOnly);
    // Permissions: 9 bits like chmod (owner, group, others × rwx).
    Q_INVOKABLE bool setPermissions(const QString& path, int bits);
    // "Opens with" → Change: the default app for that file type.
    Q_INVOKABLE void setDefaultApp(const QString& path, const QString& desktopId);
    // Sizes in Windows' format: "1.23 MB (1,290,240 bytes)".
    Q_INVOKABLE QString formatSize(double bytes) const;

signals:
    void sizeCounted(int token, double bytes, double onDisk, int files, int folders);

private:
    QThreadPool m_pool;
    int m_token = 0;
};
