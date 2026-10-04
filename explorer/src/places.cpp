#include "places.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QVariantMap>
#include <QXmlStreamReader>

#include <pwd.h>
#include <unistd.h>

#include <algorithm>

namespace {

struct UserDir {
    QStandardPaths::StandardLocation location;
    const char* icon;
};

const UserDir userDirs[] = {
    { QStandardPaths::DesktopLocation, "user-desktop" },
    { QStandardPaths::DownloadLocation, "folder-download" },
    { QStandardPaths::DocumentsLocation, "folder-documents" },
    { QStandardPaths::PicturesLocation, "folder-pictures" },
    { QStandardPaths::MusicLocation, "folder-music" },
    { QStandardPaths::MoviesLocation, "folder-videos" },
};

} // namespace

Places::Places(QObject* parent)
    : QObject(parent)
{
    loadQuickAccess();
    refreshDrives();
}

QString Places::home() const
{
    return QDir::homePath();
}

QString Places::trash() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

QString Places::userName() const
{
    if (const passwd* pw = getpwuid(getuid())) {
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return qEnvironmentVariable("USER");
}

QString Places::iconFor(const QString& path) const
{
    for (const UserDir& dir : userDirs) {
        if (QStandardPaths::writableLocation(dir.location) == path) {
            return QLatin1String(dir.icon);
        }
    }
    if (path == QDir::homePath()) {
        return QStringLiteral("user-home");
    }
    if (path == trash()) {
        return QStringLiteral("user-trash");
    }
    return QStringLiteral("folder");
}

QString Places::displayName(const QString& path) const
{
    if (path == QStringLiteral("/")) {
        return QStringLiteral("Disco locale");
    }
    if (path == trash()) {
        return QStringLiteral("Cestino");
    }
    if (path == QDir::homePath()) {
        return userName();
    }
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

void Places::loadQuickAccess()
{
    QSettings settings;
    if (settings.contains(QStringLiteral("quickAccess"))) {
        m_pinned = settings.value(QStringLiteral("quickAccess")).toStringList();
    } else {
        // All'inizio le cartelle dell'utente, come Windows.
        for (const UserDir& dir : userDirs) {
            const QString path = QStandardPaths::writableLocation(dir.location);
            if (!path.isEmpty() && path != QDir::homePath() && !m_pinned.contains(path)) {
                m_pinned.append(path);
            }
        }
    }
    m_quickAccess.clear();
    for (const QString& path : std::as_const(m_pinned)) {
        if (!QFileInfo(path).isDir()) {
            continue;
        }
        m_quickAccess.append(QVariantMap { { QStringLiteral("name"), displayName(path) },
            { QStringLiteral("path"), path }, { QStringLiteral("icon"), iconFor(path) }, { QStringLiteral("pinned"), true } });
    }
    emit quickAccessChanged();
}

void Places::saveQuickAccess()
{
    QSettings().setValue(QStringLiteral("quickAccess"), m_pinned);
    loadQuickAccess();
}

bool Places::isPinned(const QString& path) const
{
    return m_pinned.contains(path);
}

void Places::pin(const QString& path)
{
    if (!m_pinned.contains(path)) {
        m_pinned.append(path);
        saveQuickAccess();
    }
}

void Places::unpin(const QString& path)
{
    if (m_pinned.removeAll(path) > 0) {
        saveQuickAccess();
    }
}

void Places::refreshDrives()
{
    // Le unità vere (un dispositivo in /dev), una volta sola anche se
    // montate in più punti (i sottovolumi btrfs): si tiene il punto più corto.
    static const QStringList skipped { QStringLiteral("/boot"), QStringLiteral("/efi"), QStringLiteral("/boot/efi") };
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    std::sort(volumes.begin(), volumes.end(),
        [](const QStorageInfo& a, const QStorageInfo& b) { return a.rootPath().size() < b.rootPath().size(); });
    QVariantList drives;
    QStringList devices;
    for (const QStorageInfo& volume : std::as_const(volumes)) {
        const QString device = QString::fromLocal8Bit(volume.device());
        if (!volume.isValid() || !volume.isReady() || !device.startsWith(QLatin1String("/dev/"))
            || skipped.contains(volume.rootPath()) || devices.contains(device)) {
            continue;
        }
        devices.append(device);
        const QString root = volume.rootPath();
        const bool removable = root.startsWith(QLatin1String("/run/media/")) || root.startsWith(QLatin1String("/media/"));
        QString name = root == QLatin1String("/") ? QStringLiteral("Disco locale") : volume.displayName();
        if (name == root) {
            name = QFileInfo(root).fileName();
        }
        drives.append(QVariantMap {
            { QStringLiteral("name"), name },
            { QStringLiteral("path"), root },
            { QStringLiteral("icon"), removable ? QStringLiteral("drive-removable-media") : QStringLiteral("drive-harddisk") },
            { QStringLiteral("total"), double(volume.bytesTotal()) },
            { QStringLiteral("free"), double(volume.bytesAvailable()) },
            { QStringLiteral("device"), device },
            { QStringLiteral("removable"), removable },
        });
    }
    if (drives != m_drives) {
        m_drives = drives;
        emit drivesChanged();
    }
}

QVariantList Places::recentFiles(int limit) const
{
    // <bookmark href="file:///..." modified="2026-10-04T12:00:00Z">
    struct Recent {
        QString path;
        QDateTime modified;
    };
    QList<Recent> all;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));
    if (file.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("bookmark")) {
                const QUrl url(xml.attributes().value(QLatin1String("href")).toString());
                if (url.isLocalFile()) {
                    all.append({ url.toLocalFile(),
                        QDateTime::fromString(xml.attributes().value(QLatin1String("modified")).toString(), Qt::ISODate) });
                }
            }
        }
    }
    std::sort(all.begin(), all.end(), [](const Recent& a, const Recent& b) { return a.modified > b.modified; });
    static const QMimeDatabase mimes;
    QVariantList out;
    for (const Recent& r : std::as_const(all)) {
        const QFileInfo info(r.path);
        if (!info.exists() || info.isDir()) {
            continue;
        }
        const QMimeType mime = mimes.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
        out.append(QVariantMap {
            { QStringLiteral("name"), info.fileName() },
            { QStringLiteral("path"), r.path },
            { QStringLiteral("icon"), mime.iconName() },
            { QStringLiteral("modified"), r.modified.toLocalTime() },
            { QStringLiteral("location"), info.absolutePath() },
        });
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}
