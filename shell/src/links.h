// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shortcuts and Favorites, like Windows, shared by the shell's desktop and
// Explorer.
//
// A shortcut to a file or folder is a symbolic link ("photo - Shortcut.jpg");
// one to a web address is a .desktop file of type Link, as KDE and GNOME do.
// Favorites (the section of Explorer's Home) are in Explorer's settings.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QUrl>

#include <unistd.h>

// "photo.jpg" -> "photo - Shortcut.jpg"; "Documents" -> "Documents -
// Shortcut".
inline QString linkNameFor(const QFileInfo& target)
{
    const bool hasSuffix = !target.isDir() && !target.suffix().isEmpty() && !target.completeBaseName().isEmpty();
    return hasSuffix ? target.completeBaseName() + QCoreApplication::translate("Desktop", " - Shortcut.") + target.suffix()
                     : target.fileName() + QCoreApplication::translate("Desktop", " - Shortcut");
}

// A free name in the folder: "name (2).ext", like Windows.
inline QString freeNameIn(const QString& directory, const QString& name)
{
    const QDir dir(directory);
    if (!QFileInfo::exists(dir.filePath(name)) && !QFileInfo(dir.filePath(name)).isSymLink()) {
        return name;
    }
    const QFileInfo info(name);
    const QString base = info.completeBaseName().isEmpty() ? name : info.completeBaseName();
    const QString suffix = info.completeBaseName().isEmpty() || info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
    for (int i = 2;; ++i) {
        const QString candidate = base + QStringLiteral(" (%1)").arg(i) + suffix;
        if (!QFileInfo::exists(dir.filePath(candidate)) && !QFileInfo(dir.filePath(candidate)).isSymLink()) {
            return candidate;
        }
    }
}

// A shortcut to `target` inside `directory` (with the given name, or "X -
// Shortcut"). The path created, or empty if it can't be.
inline QString createLink(const QString& target, const QString& directory, const QString& name = {})
{
    const QFileInfo info(target);
    const QString chosen = freeNameIn(directory, name.isEmpty() ? linkNameFor(info) : name);
    const QString path = QDir(directory).filePath(chosen);
    if (::symlink(QFile::encodeName(info.absoluteFilePath()).constData(), QFile::encodeName(path).constData()) != 0) {
        return {};
    }
    return path;
}

// New > Shortcut: `target` is a path (also "~/...") or a web address. The path
// created; otherwise empty and `error` says why.
inline QString createShortcut(const QString& directory, const QString& target, QString name, QString* error)
{
    QString text = target.trimmed();
    if (text.startsWith(u'"') && text.endsWith(u'"') && text.size() > 1) {
        text = text.mid(1, text.size() - 2);
    }
    if (text.startsWith(QLatin1String("~/")) || text == QLatin1String("~")) {
        text = QDir::homePath() + text.mid(1);
    }
    const QUrl url(text);
    const bool web = !url.scheme().isEmpty() && url.scheme() != QLatin1String("file") && !text.startsWith(u'/');
    if (!web) {
        const QString local = url.isLocalFile() ? url.toLocalFile() : text;
        if (!QFileInfo::exists(local)) {
            if (error) {
                *error = QCoreApplication::translate("Desktop", "Can't find the file \"%1\".").arg(local);
            }
            return {};
        }
        const QFileInfo info(local);
        if (!name.isEmpty() && !info.isDir() && !info.suffix().isEmpty() && !name.endsWith(u'.' + info.suffix())) {
            name += u'.' + info.suffix(); // the extension stays: the file type is recognized
        }
        const QString path = createLink(local, directory, name.isEmpty() ? info.fileName() : name);
        if (path.isEmpty() && error) {
            *error = QCoreApplication::translate("Desktop", "Couldn't create the shortcut here.");
        }
        return path;
    }
    if (name.isEmpty()) {
        name = url.host().isEmpty() ? text : url.host();
    }
    const QString file = freeNameIn(directory, name + QStringLiteral(".desktop"));
    const QString path = QDir(directory).filePath(file);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QCoreApplication::translate("Desktop", "Couldn't create the shortcut here.");
        }
        return {};
    }
    out.write("[Desktop Entry]\nType=Link\nName=" + name.toUtf8() + "\nURL=" + url.toString().toUtf8()
        + "\nIcon=text-html\n");
    return path;
}

// ------------------------------------------------------------ Favorites --

inline QStringList favoriteFiles()
{
    QSettings settings(QStringLiteral("Vela"), QStringLiteral("vela-files"));
    settings.sync();
    return settings.value(QStringLiteral("favorites")).toStringList();
}

inline bool isFavorite(const QString& path)
{
    return favoriteFiles().contains(path);
}

inline void setFavorite(const QString& path, bool on)
{
    QSettings settings(QStringLiteral("Vela"), QStringLiteral("vela-files"));
    settings.sync();
    QStringList list = settings.value(QStringLiteral("favorites")).toStringList();
    list.removeAll(path);
    if (on) {
        list.prepend(path);
    }
    settings.setValue(QStringLiteral("favorites"), list);
    settings.sync();
}
