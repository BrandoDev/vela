// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "defaultapps.h"

#include "appmodel.h"
#include "mimeapps.h"

#include <QCoreApplication>
#include <QCollator>
#include <QMimeDatabase>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace {

struct Category {
    const char* key;
    const char* label;
    const char* icon;
    QStringList mimes; // the first decides which is the current app; all are set
};

const QList<Category>& categoryList()
{
    static const QList<Category> list {
        { "web", QT_TRANSLATE_NOOP("DefaultApps", "Web browser"), "internet-web-browser",
            { "x-scheme-handler/http", "x-scheme-handler/https", "text/html", "application/xhtml+xml" } },
        { "mail", QT_TRANSLATE_NOOP("DefaultApps", "Email"), "internet-mail", { "x-scheme-handler/mailto" } },
        { "music", QT_TRANSLATE_NOOP("DefaultApps", "Music"), "multimedia-audio-player",
            { "audio/mpeg", "audio/flac", "audio/ogg", "audio/x-vorbis+ogg", "audio/x-opus+ogg", "audio/x-wav",
                "audio/mp4", "audio/aac", "audio/x-ms-wma" } },
        { "video", QT_TRANSLATE_NOOP("DefaultApps", "Video"), "multimedia-video-player",
            { "video/mp4", "video/x-matroska", "video/webm", "video/x-msvideo", "video/quicktime", "video/mpeg",
                "video/ogg", "video/x-flv" } },
        { "image", QT_TRANSLATE_NOOP("DefaultApps", "Photos"), "image-x-generic",
            { "image/jpeg", "image/png", "image/webp", "image/gif", "image/avif", "image/heif", "image/bmp",
                "image/tiff" } },
        { "pdf", QT_TRANSLATE_NOOP("DefaultApps", "PDF"), "application-pdf", { "application/pdf" } },
        { "text", QT_TRANSLATE_NOOP("DefaultApps", "Text files"), "accessories-text-editor", { "text/plain" } },
        { "files", QT_TRANSLATE_NOOP("DefaultApps", "Folders"), "system-file-manager", { "inode/directory" } },
        { "archive", QT_TRANSLATE_NOOP("DefaultApps", "Compressed archives"), "package-x-generic",
            { "application/zip", "application/x-7z-compressed", "application/x-tar", "application/x-compressed-tar",
                "application/x-xz-compressed-tar", "application/x-bzip2-compressed-tar", "application/vnd.rar" } },
        { "document", QT_TRANSLATE_NOOP("DefaultApps", "Documents"), "x-office-document",
            { "application/vnd.oasis.opendocument.text",
                "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "application/msword",
                "application/rtf" } },
        { "spreadsheet", QT_TRANSLATE_NOOP("DefaultApps", "Spreadsheets"), "x-office-spreadsheet",
            { "application/vnd.oasis.opendocument.spreadsheet",
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "application/vnd.ms-excel",
                "text/csv" } },
        { "presentation", QT_TRANSLATE_NOOP("DefaultApps", "Presentations"), "x-office-presentation",
            { "application/vnd.oasis.opendocument.presentation",
                "application/vnd.openxmlformats-officedocument.presentationml.presentation",
                "application/vnd.ms-powerpoint" } },
        { "ebook", QT_TRANSLATE_NOOP("DefaultApps", "E-books"), "application-epub+zip",
            { "application/epub+zip", "application/x-mobipocket-ebook" } },
        { "calendar", QT_TRANSLATE_NOOP("DefaultApps", "Calendar"), "office-calendar", { "text/calendar" } },
        { "torrent", QT_TRANSLATE_NOOP("DefaultApps", "Torrent"), "application-x-bittorrent", { "application/x-bittorrent", "x-scheme-handler/magnet" } },
    };
    return list;
}

const Category* findCategory(const QString& key)
{
    for (const Category& c : categoryList()) {
        if (key == QLatin1String(c.key)) {
            return &c;
        }
    }
    return nullptr;
}

const QMimeDatabase& mimeDatabase()
{
    static const QMimeDatabase database;
    return database;
}

// The type and those it descends from (an app for text/plain also opens
// text/x-python), with aliases.
QStringList withAncestors(const QString& mime)
{
    QStringList out { mime };
    const QMimeType type = mimeDatabase().mimeTypeForName(mime);
    if (type.isValid()) {
        out << type.name() << type.aliases() << type.allAncestors();
    }
    out.removeDuplicates();
    return out;
}

// A type's description and extensions, to show it.
QVariantMap describe(const QString& mime)
{
    if (mime.startsWith(QLatin1String("x-scheme-handler/"))) {
        const QString scheme = mime.mid(17);
        return { { QStringLiteral("label"), QCoreApplication::translate("DefaultApps", "%1 links:").arg(scheme.toUpper()) },
            { QStringLiteral("patterns"), scheme + u':' }, { QStringLiteral("icon"), QStringLiteral("text-html") } };
    }
    const QMimeType type = mimeDatabase().mimeTypeForName(mime);
    QStringList patterns;
    for (const QString& suffix : type.suffixes()) {
        patterns.append(u'.' + suffix);
    }
    QString label = type.isValid() ? type.comment() : mime;
    if (!label.isEmpty()) {
        label[0] = label[0].toUpper();
    }
    return { { QStringLiteral("label"), label.isEmpty() ? mime : label },
        { QStringLiteral("patterns"), patterns.mid(0, 4).join(QStringLiteral(", ")) },
        { QStringLiteral("icon"), type.isValid() ? type.iconName() : QStringLiteral("unknown") } };
}

QVariantList sortedByName(QVariantList list)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&](const QVariant& a, const QVariant& b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(),
                   b.toMap().value(QStringLiteral("name")).toString())
            < 0;
    });
    return list;
}

} // namespace

DefaultApps::DefaultApps(AppModel* apps, QObject* parent)
    : QObject(parent)
    , m_apps(apps)
{
}

QVariantMap DefaultApps::appInfo(const QString& appId) const
{
    const AppModel::Entry* entry = m_apps->find(appId);
    if (!entry) {
        return {};
    }
    return { { QStringLiteral("id"), entry->id }, { QStringLiteral("name"), entry->name },
        { QStringLiteral("icon"), entry->icon } };
}

// The apps opening one of those types (or a parent), plus those associated by
// hand; the current one first, the others by name.
QVariantList DefaultApps::candidatesFor(const QStringList& mimes, const QString& current) const
{
    QSet<QString> accepted;
    for (const QString& mime : mimes) {
        for (const QString& m : withAncestors(mime)) {
            accepted.insert(m);
        }
    }
    QVariantList out;
    QSet<QString> seen;
    QSet<QString> names;
    for (const AppModel::Entry& e : m_apps->all()) {
        const bool opens = std::any_of(
            e.mimeTypes.cbegin(), e.mimeTypes.cend(), [&](const QString& m) { return accepted.contains(m); });
        if (!opens || e.id == current || names.contains(e.name)) {
            continue;
        }
        seen.insert(e.id);
        names.insert(e.name);
        out.append(appInfo(e.id));
    }
    for (const QString& mime : mimes) {
        for (const QString& id : MimeApps::addedFor(mime)) {
            const QVariantMap info = appInfo(id);
            if (!info.isEmpty() && id != current && !seen.contains(id) && !names.contains(info.value(QStringLiteral("name")).toString())) {
                seen.insert(id);
                names.insert(info.value(QStringLiteral("name")).toString());
                out.append(info);
            }
        }
    }
    out = sortedByName(out);
    if (const QVariantMap info = appInfo(current); !info.isEmpty()) {
        out.prepend(info);
    }
    return out;
}

QVariantList DefaultApps::categories() const
{
    QVariantList out;
    for (const Category& c : categoryList()) {
        const QString current = MimeApps::defaultFor(c.mimes.first());
        out.append(QVariantMap { { QStringLiteral("key"), QLatin1String(c.key) },
            { QStringLiteral("label"), QCoreApplication::translate("DefaultApps", c.label) }, { QStringLiteral("icon"), QLatin1String(c.icon) },
            { QStringLiteral("current"), appInfo(current) },
            { QStringLiteral("candidates"), candidatesFor(c.mimes, current) } });
    }
    return out;
}

void DefaultApps::setDefault(const QString& key, const QString& appId)
{
    if (const Category* category = findCategory(key)) {
        MimeApps::setDefault(category->mimes, appId);
        emit changed();
    }
}

QVariantMap DefaultApps::lookup(const QString& text) const
{
    QString query = text.trimmed().toLower();
    if (query.isEmpty()) {
        return { { QStringLiteral("found"), false } };
    }
    QString mime;
    static const QStringList schemes { "http", "https", "mailto", "tel", "magnet", "ftp", "sftp", "irc", "ssh",
        "webcal", "sms", "geo", "steam", "discord", "tg", "zoommtg", "spotify" };
    if (query.endsWith(u':')) {
        query.chop(1);
    }
    if (query.startsWith(QLatin1String("x-scheme-handler/"))) {
        mime = query;
    } else if (query.contains(u'/')) {
        const QMimeType type = mimeDatabase().mimeTypeForName(query);
        mime = type.isValid() ? type.name() : QString();
    } else if (schemes.contains(query)) {
        mime = QStringLiteral("x-scheme-handler/") + query;
    } else {
        if (query.startsWith(u'.')) {
            query.remove(0, 1);
        }
        const QList<QMimeType> types = mimeDatabase().mimeTypesForFileName(QStringLiteral("x.") + query);
        mime = types.isEmpty() ? QString() : types.first().name();
    }
    if (mime.isEmpty()) {
        return { { QStringLiteral("found"), false } };
    }
    QVariantMap out = describe(mime);
    const QString current = MimeApps::defaultFor(mime);
    out.insert(QStringLiteral("found"), true);
    out.insert(QStringLiteral("mime"), mime);
    out.insert(QStringLiteral("current"), appInfo(current));
    out.insert(QStringLiteral("candidates"), candidatesFor({ mime }, current));
    return out;
}

void DefaultApps::setDefaultForType(const QString& mime, const QString& appId)
{
    MimeApps::setDefault({ mime }, appId);
    emit changed();
}

QVariantList DefaultApps::apps(const QString& filter) const
{
    QVariantList out;
    QSet<QString> names;
    for (const AppModel::Entry& e : m_apps->all()) {
        if (e.mimeTypes.isEmpty() || names.contains(e.name)
            || (!filter.isEmpty() && !e.name.contains(filter, Qt::CaseInsensitive))) {
            continue;
        }
        names.insert(e.name);
        QVariantMap info = appInfo(e.id);
        info.insert(QStringLiteral("count"), int(e.mimeTypes.size()));
        out.append(info);
    }
    return sortedByName(out);
}

QVariantList DefaultApps::typesOf(const QString& appId) const
{
    const AppModel::Entry* entry = m_apps->find(appId);
    if (!entry) {
        return {};
    }
    QVariantList out;
    for (const QString& mime : entry->mimeTypes) {
        QVariantMap type = describe(mime);
        const QString current = MimeApps::defaultFor(mime);
        type.insert(QStringLiteral("mime"), mime);
        type.insert(QStringLiteral("isDefault"), current == appId);
        type.insert(QStringLiteral("current"), appInfo(current));
        out.append(type);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("label")).toString().compare(
                   b.toMap().value(QStringLiteral("label")).toString(), Qt::CaseInsensitive)
            < 0;
    });
    return out;
}

void DefaultApps::setDefaultForAll(const QString& appId)
{
    if (const AppModel::Entry* entry = m_apps->find(appId)) {
        MimeApps::setDefault(entry->mimeTypes, appId);
        emit changed();
    }
}

void DefaultApps::setSelectedApp(const QString& appId)
{
    if (m_selected != appId) {
        m_selected = appId;
        emit selectedAppChanged();
    }
}

QString DefaultApps::selectedName() const
{
    const AppModel::Entry* entry = m_apps->find(m_selected);
    return entry ? entry->name : QString();
}

QString DefaultApps::selectedIcon() const
{
    const AppModel::Entry* entry = m_apps->find(m_selected);
    return entry ? entry->icon : QString();
}
