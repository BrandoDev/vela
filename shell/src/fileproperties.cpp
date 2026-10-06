// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fileproperties.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QRunnable>

#include <sys/stat.h>
#include <unistd.h>

namespace {

struct Count {
    qint64 bytes = 0;
    qint64 onDisk = 0;
    int files = 0;
    int folders = 0;
};

// Dimensione reale su disco: i blocchi occupati (file sparsi, blocchi pieni).
qint64 diskUsage(const QString& path)
{
    struct stat st {};
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? qint64(st.st_blocks) * 512 : 0;
}

void countInto(const QString& path, Count& count)
{
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink()) {
        count.bytes += info.size();
        count.onDisk += diskUsage(path);
        ++count.files;
        return;
    }
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo entry = it.fileInfo();
        if (entry.isDir() && !entry.isSymLink()) {
            ++count.folders;
        } else {
            count.bytes += entry.size();
            count.onDisk += diskUsage(entry.filePath());
            ++count.files;
        }
    }
}

QString dateText(const QDateTime& when)
{
    return when.isValid() ? QLocale().toString(when, QStringLiteral("d MMMM yyyy, HH:mm:ss")) : QString();
}

} // namespace

FileProperties::FileProperties(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

QString FileProperties::formatSize(double bytes) const
{
    const QLocale locale;
    const QString shortForm = locale.formattedDataSize(qint64(bytes), 2, QLocale::DataSizeTraditionalFormat);
    return QStringLiteral("%1 (%2 byte)").arg(shortForm, locale.toString(qint64(bytes)));
}

QVariantMap FileProperties::describe(const QStringList& paths)
{
    QVariantMap out;
    if (paths.isEmpty()) {
        return out;
    }
    static const QMimeDatabase mimes;
    const int token = ++m_token;
    out.insert(QStringLiteral("token"), token);
    out.insert(QStringLiteral("count"), int(paths.size()));

    bool anyDir = false;
    QStringList types;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        anyDir = anyDir || info.isDir();
        const QString type = info.isDir() ? QStringLiteral("Cartella di file") : mimes.mimeTypeForFile(info).comment();
        if (!types.contains(type)) {
            types.append(type);
        }
    }
    out.insert(QStringLiteral("type"), types.size() == 1 ? types.first() : QStringLiteral("Più tipi"));
    out.insert(QStringLiteral("hasFolders"), anyDir);

    const QFileInfo first(paths.first());
    out.insert(QStringLiteral("location"), first.absolutePath());
    bool readOnly = true;
    for (const QString& path : paths) {
        readOnly = readOnly && !(QFileInfo(path).permissions() & QFileDevice::WriteOwner);
    }
    out.insert(QStringLiteral("readOnly"), readOnly);

    if (paths.size() == 1) {
        const QMimeType mime = mimes.mimeTypeForFile(first);
        out.insert(QStringLiteral("name"), first.fileName());
        out.insert(QStringLiteral("isDir"), first.isDir());
        out.insert(QStringLiteral("mime"), mime.name());
        out.insert(QStringLiteral("icon"), first.isDir() ? QStringLiteral("folder") : mime.iconName());
        if (!first.isDir() && !first.suffix().isEmpty()) {
            out.insert(QStringLiteral("type"), QStringLiteral("%1 (.%2)").arg(mime.comment(), first.suffix()));
        }
        out.insert(QStringLiteral("hidden"), first.fileName().startsWith(u'.'));
        out.insert(QStringLiteral("created"), dateText(first.birthTime()));
        out.insert(QStringLiteral("modified"), dateText(first.lastModified()));
        out.insert(QStringLiteral("accessed"), dateText(first.lastRead()));
        out.insert(QStringLiteral("owner"), first.owner());
        out.insert(QStringLiteral("group"), first.group());
        // I 9 bit di chmod (proprietario, gruppo, altri × lettura, scrittura, esecuzione).
        struct stat st {};
        const int bits = ::stat(QFile::encodeName(first.absoluteFilePath()).constData(), &st) == 0
            ? int(st.st_mode & 0777)
            : 0;
        out.insert(QStringLiteral("permissions"), bits);
        out.insert(QStringLiteral("mine"), first.ownerId() == ::getuid());
        // Dettagli: le immagini dicono quanto sono grandi.
        QImageReader reader(first.absoluteFilePath());
        if (!first.isDir() && reader.canRead()) {
            const QSize size = reader.size();
            if (size.isValid()) {
                out.insert(QStringLiteral("imageSize"), QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
            }
        }
        out.insert(QStringLiteral("mimeName"), mime.name());
    } else {
        out.insert(QStringLiteral("name"), QStringLiteral("%1 elementi").arg(paths.size()));
        out.insert(QStringLiteral("icon"), QStringLiteral("document-multiple"));
    }

    // Le dimensioni: subito per i file soli, in un thread se ci sono cartelle.
    if (!anyDir) {
        Count count;
        for (const QString& path : paths) {
            countInto(path, count);
        }
        out.insert(QStringLiteral("bytes"), double(count.bytes));
        out.insert(QStringLiteral("onDisk"), double(count.onDisk));
        out.insert(QStringLiteral("files"), count.files);
        out.insert(QStringLiteral("folders"), 0);
    } else {
        QPointer<FileProperties> self(this);
        m_pool.start([self, paths, token] {
            Count count;
            for (const QString& path : paths) {
                countInto(path, count);
            }
            QMetaObject::invokeMethod(
                self.data(),
                [self, token, count] {
                    if (self) {
                        emit self->sizeCounted(token, double(count.bytes), double(count.onDisk), count.files,
                            count.folders);
                    }
                },
                Qt::QueuedConnection);
        });
    }
    return out;
}

bool FileProperties::setReadOnly(const QStringList& paths, bool readOnly)
{
    bool ok = true;
    for (const QString& path : paths) {
        QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        if (readOnly) {
            permissions &= ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup
                | QFileDevice::WriteOther);
        } else {
            permissions |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
        }
        ok = QFile::setPermissions(path, permissions) && ok;
    }
    return ok;
}

bool FileProperties::setPermissions(const QString& path, int bits)
{
    struct stat st {};
    const QByteArray name = QFile::encodeName(path);
    if (::stat(name.constData(), &st) != 0) {
        return false;
    }
    // Si cambiano solo i 9 bit: setuid, setgid e sticky restano com'erano.
    return ::chmod(name.constData(), (st.st_mode & ~mode_t(0777)) | mode_t(bits & 0777)) == 0;
}

void FileProperties::setDefaultApp(const QString& path, const QString& desktopId)
{
    static const QMimeDatabase mimes;
    const QString mime = mimes.mimeTypeForFile(path).name();
    // xdg-mime scrive mimeapps.list, lo stesso che leggono KDE e GNOME.
    QProcess::startDetached(QStringLiteral("xdg-mime"), { QStringLiteral("default"), desktopId, mime });
}
