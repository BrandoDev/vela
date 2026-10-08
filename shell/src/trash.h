// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The FreeDesktop home trash has files/ and matching info/*.trashinfo records.
// Remove only entries in files/, never the trash directory itself, and keep
// metadata if an entry could not be deleted (so it can still be restored).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace vela::trash {

inline QString homeDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
}

// Returns the names of any entries that could not be removed. The caller
// decides how to report them and may run this on a background thread.
inline QStringList empty(const QString& directory)
{
    const QDir files(QDir(directory).filePath(QStringLiteral("files")));
    const QDir info(QDir(directory).filePath(QStringLiteral("info")));
    const QDir::Filters entries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
    QStringList failed;

    for (const QFileInfo& entry : files.entryInfoList(entries)) {
        // Never follow symlinks, including symlinks to directories.
        const bool removed = entry.isDir() && !entry.isSymLink()
            ? QDir(entry.absoluteFilePath()).removeRecursively()
            : QFile::remove(entry.absoluteFilePath());
        if (!removed) {
            failed.append(entry.fileName());
        }
    }

    // Remove metadata only when the corresponding entry is gone. This also
    // cleans orphaned .trashinfo records left by previously interrupted work.
    for (const QFileInfo& record : info.entryInfoList(entries)) {
        if (!record.fileName().endsWith(QLatin1String(".trashinfo"))) {
            continue;
        }
        const QString name = record.fileName().chopped(10); // ".trashinfo"
        const QFileInfo original(files.filePath(name));
        if (original.exists() || original.isSymLink()) {
            continue;
        }
        if (!QFile::remove(record.absoluteFilePath())) {
            failed.append(record.fileName());
        }
    }
    return failed;
}

inline QStringList emptyHome()
{
    return empty(homeDirectory());
}

} // namespace vela::trash
