// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMimeDatabase>
#include <QPair>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>

// Default apps, as the freedesktop "Association between MIME types and
// applications" spec says: the mimeapps.list files. KDE, GNOME, Firefox,
// Chromium and xdg-open read them: a choice made here applies everywhere.
//
// Reading order: for each configuration directory (the user's first, then the
// system ones) the current desktop's file ("vela-mimeapps.list",
// "kde-mimeapps.list", from XDG_CURRENT_DESKTOP) and then "mimeapps.list";
// finally applications/mimeapps.list in the data directories. An app listed
// but not installed is skipped.
namespace MimeApps {

inline QStringList desktops()
{
    QStringList out;
    for (const QString& name : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts)) {
        out.append(name.toLower());
    }
    return out;
}

inline QString userFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
}

inline QStringList files()
{
    QStringList out;
    const QStringList configDirs = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    for (const QString& dir : configDirs) {
        for (const QString& desktop : desktops()) {
            out.append(dir + u'/' + desktop + QStringLiteral("-mimeapps.list"));
        }
        out.append(dir + QStringLiteral("/mimeapps.list"));
    }
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        for (const QString& desktop : desktops()) {
            out.append(dir + QStringLiteral("/applications/") + desktop + QStringLiteral("-mimeapps.list"));
        }
        out.append(dir + QStringLiteral("/applications/mimeapps.list"));
    }
    return out;
}

// The values of `key` in group `group` of a file ("a.desktop;b.desktop;").
inline QStringList values(const QString& path, const QString& group, const QString& key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    bool inGroup = false;
    QTextStream stream(&file);
    QString line;
    while (stream.readLineInto(&line)) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(u'[')) {
            inGroup = trimmed == u'[' + group + u']';
        } else if (inGroup && trimmed.startsWith(key + u'=')) {
            QStringList ids;
            for (const QString& id : trimmed.mid(key.size() + 1).split(u';', Qt::SkipEmptyParts)) {
                ids.append(id.trimmed());
            }
            return ids;
        }
    }
    return {};
}

// All the names of a type: the given one, the canonical one and the aliases
// ("video/x-matroska" and "video/matroska" are the same type, and
// mimeapps.list files use one or the other).
inline QStringList names(const QString& mime)
{
    QStringList out { mime };
    static const QMimeDatabase database;
    const QMimeType type = database.mimeTypeForName(mime);
    if (type.isValid()) {
        out << type.name() << type.aliases();
    }
    out.removeDuplicates();
    return out;
}

inline bool installed(const QString& desktopId)
{
    return !desktopId.isEmpty()
        && !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId).isEmpty();
}

// The default app for a type (.desktop id), or empty.
inline QString defaultFor(const QString& mime)
{
    const QStringList keys = names(mime);
    for (const QString& path : files()) {
        for (const QString& key : keys) {
            for (const QString& id : values(path, QStringLiteral("Default Applications"), key)) {
                if (installed(id)) {
                    return id;
                }
            }
        }
    }
    return {};
}

// Apps associated with a type by hand ("Added Associations" group), even if
// their .desktop doesn't declare it.
inline QStringList addedFor(const QString& mime)
{
    QStringList out;
    for (const QString& path : files()) {
        for (const QString& key : names(mime)) {
            for (const QString& id : values(path, QStringLiteral("Added Associations"), key)) {
                if (!out.contains(id) && installed(id)) {
                    out.append(id);
                }
            }
        }
    }
    return out;
}

// Writes `group`'s keys (key -> value) to `path`, leaving the rest of the file
// as it was. With `onlyExisting` only keys already present change. Atomically
// (QSaveFile).
inline bool writeKeys(const QString& path, const QString& group, const QList<QPair<QString, QString>>& keys,
    bool onlyExisting)
{
    QStringList lines;
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            QString line;
            while (stream.readLineInto(&line)) {
                lines.append(line);
            }
        } else if (onlyExisting) {
            return true;
        }
    }
    QList<QPair<QString, QString>> pending = keys;
    qsizetype groupStart = -1;
    qsizetype groupEnd = lines.size(); // the line after the group's last one
    bool changed = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith(u'[')) {
            if (groupStart >= 0 && groupEnd == lines.size()) {
                groupEnd = i;
            }
            if (trimmed == u'[' + group + u']') {
                groupStart = i;
                groupEnd = lines.size();
            }
            continue;
        }
        if (groupStart < 0 || groupEnd != lines.size()) {
            continue;
        }
        for (qsizetype k = 0; k < pending.size(); ++k) {
            if (trimmed.startsWith(pending.at(k).first + u'=')) {
                lines[i] = pending.at(k).first + u'=' + pending.at(k).second;
                pending.removeAt(k);
                changed = true;
                break;
            }
        }
    }
    if (!onlyExisting && !pending.isEmpty()) {
        QStringList added;
        for (const auto& [key, value] : std::as_const(pending)) {
            added.append(key + u'=' + value);
        }
        if (groupStart < 0) {
            if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) {
                lines.append(QString());
            }
            lines.append(u'[' + group + u']');
            lines.append(added);
        } else {
            // Before the empty lines at the end of the group.
            qsizetype at = groupEnd;
            while (at > groupStart + 1 && lines.at(at - 1).trimmed().isEmpty()) {
                --at;
            }
            for (qsizetype k = 0; k < added.size(); ++k) {
                lines.insert(at + k, added.at(k));
            }
        }
        changed = true;
    }
    if (!changed) {
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(lines.join(u'\n').toUtf8() + '\n');
    return file.commit();
}

// `appId` becomes the default app for the given types: in the user's
// mimeapps.list (Default Applications, and first in Added Associations, so it
// stays offered even if its .desktop doesn't declare the type). If the current
// desktop's file (such as kde-mimeapps.list, written by Plasma) already has a
// choice for those types, it's updated: otherwise the old one would win.
inline bool setDefault(const QStringList& mimes, const QString& appId)
{
    if (mimes.isEmpty() || appId.isEmpty()) {
        return false;
    }
    QStringList all;
    for (const QString& mime : mimes) {
        all << names(mime); // every name of the type, so whoever uses either finds it
    }
    all.removeDuplicates();
    QList<QPair<QString, QString>> defaults;
    QList<QPair<QString, QString>> added;
    for (const QString& mime : std::as_const(all)) {
        defaults.append({ mime, appId + u';' });
        QStringList ids { appId };
        for (const QString& id : values(userFile(), QStringLiteral("Added Associations"), mime)) {
            if (id != appId) {
                ids.append(id);
            }
        }
        added.append({ mime, ids.join(u';') + u';' });
    }
    bool ok = writeKeys(userFile(), QStringLiteral("Default Applications"), defaults, false)
        && writeKeys(userFile(), QStringLiteral("Added Associations"), added, false);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const QString& desktop : desktops()) {
        ok = writeKeys(config + u'/' + desktop + QStringLiteral("-mimeapps.list"), QStringLiteral("Default Applications"),
                 defaults, true)
            && ok;
    }
    return ok;
}

} // namespace MimeApps
