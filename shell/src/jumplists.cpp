// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jumplists.h"

#include "appmodel.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace {

struct Recent {
    QString url;
    QDateTime when;
    QStringList appNames; // bookmark:application name
    QStringList programs; // il primo pezzo di bookmark:application exec
};

QList<Recent> readRecentlyUsed()
{
    QList<Recent> out;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/recently-used.xbel"));
    if (!file.open(QIODevice::ReadOnly)) {
        return out;
    }
    QXmlStreamReader xml(&file);
    Recent* current = nullptr;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QXmlStreamAttributes attributes = xml.attributes();
        if (xml.name() == QLatin1String("bookmark")) {
            Recent recent;
            recent.url = attributes.value(QLatin1String("href")).toString();
            recent.when = QDateTime::fromString(attributes.value(QLatin1String("visited")).toString(), Qt::ISODate);
            const QDateTime modified
                = QDateTime::fromString(attributes.value(QLatin1String("modified")).toString(), Qt::ISODate);
            if (!recent.when.isValid() || (modified.isValid() && modified > recent.when)) {
                recent.when = modified;
            }
            out.append(recent);
            current = &out.last();
        } else if (current && xml.name() == QLatin1String("application")) {
            current->appNames << attributes.value(QLatin1String("name")).toString();
            // exec="'kate -b %u'": il programma, senza percorso né apici.
            QString exec = attributes.value(QLatin1String("exec")).toString();
            exec.remove(u'\'');
            current->programs << exec.section(u' ', 0, 0).section(u'/', -1);
            // Il momento dell'ultimo uso da parte di quest'app.
            const QDateTime modified
                = QDateTime::fromString(attributes.value(QLatin1String("modified")).toString(), Qt::ISODate);
            if (modified.isValid() && modified > current->when) {
                current->when = modified;
            }
        }
    }
    return out;
}

QString settingsKey(const char* group, const QString& desktopId)
{
    // Le chiavi di QSettings non amano '/': negli id non c'è, ma meglio esserne certi.
    return QLatin1String(group) + u'/' + QString(desktopId).replace(u'/', u'_');
}

} // namespace

JumpLists::JumpLists(AppModel* apps, QObject* parent)
    : QObject(parent)
    , m_apps(apps)
{
}

QVariantMap JumpLists::describe(const QString& url) const
{
    const QUrl u(url);
    static const QMimeDatabase mimes;
    const QString path = u.isLocalFile() ? u.toLocalFile() : QString();
    const QString name = path.isEmpty() ? u.fileName() : QFileInfo(path).fileName();
    const QString icon = path.isEmpty() ? QStringLiteral("text-html") : mimes.mimeTypeForFile(path).iconName();
    return { { QStringLiteral("url"), url }, { QStringLiteral("name"), name.isEmpty() ? url : name },
        { QStringLiteral("icon"), icon } };
}

QVariantList JumpLists::recent(const QString& desktopId, int limit) const
{
    QVariantList out;
    const AppModel::Entry* app = m_apps->find(desktopId);
    if (!app) {
        return out;
    }
    const QString baseId = desktopId.endsWith(QLatin1String(".desktop")) ? desktopId.chopped(8) : desktopId;
    const QString program = m_apps->program(desktopId);

    QSettings settings;
    const QStringList pinnedUrls = settings.value(settingsKey("jumplist-pinned", desktopId)).toStringList();
    const QStringList forgotten = settings.value(settingsKey("jumplist-forgotten", desktopId)).toStringList();

    QList<Recent> recents = readRecentlyUsed();
    std::sort(recents.begin(), recents.end(), [](const Recent& a, const Recent& b) { return a.when > b.when; });
    for (const Recent& r : std::as_const(recents)) {
        const bool mine = r.appNames.contains(baseId, Qt::CaseInsensitive)
            || r.appNames.contains(app->name, Qt::CaseInsensitive)
            || (!program.isEmpty() && r.programs.contains(program, Qt::CaseInsensitive));
        if (!mine || pinnedUrls.contains(r.url) || forgotten.contains(r.url)) {
            continue;
        }
        const QUrl url(r.url);
        if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) {
            continue; // cancellato o spostato
        }
        out.append(describe(r.url));
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

QVariantList JumpLists::pinned(const QString& desktopId) const
{
    QVariantList out;
    const QStringList urls = QSettings().value(settingsKey("jumplist-pinned", desktopId)).toStringList();
    for (const QString& url : urls) {
        out.append(describe(url));
    }
    return out;
}

void JumpLists::setPinned(const QString& desktopId, const QString& url, bool pinned)
{
    QSettings settings;
    const QString key = settingsKey("jumplist-pinned", desktopId);
    QStringList urls = settings.value(key).toStringList();
    urls.removeAll(url);
    if (pinned) {
        urls.append(url);
    }
    settings.setValue(key, urls);
}

void JumpLists::forget(const QString& desktopId, const QString& url)
{
    setPinned(desktopId, url, false);
    QSettings settings;
    const QString key = settingsKey("jumplist-forgotten", desktopId);
    QStringList urls = settings.value(key).toStringList();
    if (!urls.contains(url)) {
        urls.append(url);
        settings.setValue(key, urls);
    }
}
