// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "defaultapps.h"

#include "appmodel.h"

#include <QProcess>
#include <QSet>
#include <QVariantMap>

namespace {

struct Category {
    const char* key;
    const char* label;
    const char* icon;
    QStringList mimes; // il primo decide qual è l'app attuale; si impostano tutti
};

const QList<Category>& categoryList()
{
    static const QList<Category> list {
        { "web", "Browser web", "internet-web-browser",
            { "x-scheme-handler/http", "x-scheme-handler/https", "text/html", "application/xhtml+xml" } },
        { "mail", "Posta elettronica", "internet-mail", { "x-scheme-handler/mailto" } },
        { "music", "Lettore musicale", "multimedia-audio-player",
            { "audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav", "audio/mp4", "audio/x-opus+ogg" } },
        { "video", "Lettore video", "multimedia-video-player",
            { "video/mp4", "video/x-matroska", "video/webm", "video/x-msvideo", "video/quicktime" } },
        { "image", "Visualizzatore foto", "image-x-generic",
            { "image/jpeg", "image/png", "image/webp", "image/gif", "image/avif", "image/heif" } },
        { "pdf", "PDF", "application-pdf", { "application/pdf" } },
        { "text", "Editor di testo", "accessories-text-editor", { "text/plain" } },
        { "files", "Esplora file", "system-file-manager", { "inode/directory" } },
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

} // namespace

DefaultApps::DefaultApps(AppModel* apps, QObject* parent)
    : QObject(parent)
    , m_apps(apps)
{
}

QVariantList DefaultApps::categories() const
{
    QVariantList out;
    for (const Category& c : categoryList()) {
        QVariantMap current;
        if (const AppModel::Entry* entry = m_apps->find(defaultAppFor(c.mimes.first()))) {
            current = { { QStringLiteral("id"), entry->id }, { QStringLiteral("name"), entry->name },
                { QStringLiteral("icon"), entry->icon } };
        }
        out.append(QVariantMap { { QStringLiteral("key"), QLatin1String(c.key) },
            { QStringLiteral("label"), QString::fromUtf8(c.label) }, { QStringLiteral("icon"), QLatin1String(c.icon) },
            { QStringLiteral("current"), current } });
    }
    return out;
}

QVariantList DefaultApps::candidates(const QString& key) const
{
    const Category* category = findCategory(key);
    if (!category) {
        return {};
    }
    QVariantList out;
    QSet<QString> names;
    for (const AppModel::Entry& e : m_apps->all()) {
        const bool opens = e.mimeTypes.contains(category->mimes.first());
        if (!opens || names.contains(e.name)) {
            continue;
        }
        names.insert(e.name);
        out.append(QVariantMap { { QStringLiteral("id"), e.id }, { QStringLiteral("name"), e.name },
            { QStringLiteral("icon"), e.icon } });
    }
    return out;
}

void DefaultApps::setDefault(const QString& key, const QString& appId)
{
    const Category* category = findCategory(key);
    if (!category || appId.isEmpty()) {
        return;
    }
    QProcess process;
    process.start(QStringLiteral("xdg-mime"), QStringList { QStringLiteral("default"), appId } + category->mimes);
    process.waitForFinished(5000);
    emit changed();
}
