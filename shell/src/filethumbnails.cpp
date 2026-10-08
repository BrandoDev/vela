// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filethumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMimeDatabase>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {

// freedesktop spec: normal 128, large 256, x-large 512.
struct CacheSize {
    const char* dir;
    int pixels;
};
constexpr CacheSize cacheSizes[] { { "normal", 128 }, { "large", 256 }, { "x-large", 512 } };

constexpr qint64 maxImageBytes = 64 * 1024 * 1024; // beyond this, no thumbnail (too slow)

QImage thumbnailFor(const QString& path, int wanted)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {};
    }
    const QByteArray uri = QUrl::fromLocalFile(info.absoluteFilePath()).toEncoded();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex());
    const QString mtime = QString::number(info.lastModified().toSecsSinceEpoch());
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/thumbnails/");

    // The right directory for the requested size (the smallest large enough).
    const CacheSize* size = &cacheSizes[2];
    for (const CacheSize& s : cacheSizes) {
        if (wanted <= s.pixels) {
            size = &s;
            break;
        }
    }

    // Already made (by us or another app), and still valid: from any directory
    // large enough.
    for (const CacheSize& s : cacheSizes) {
        if (s.pixels < size->pixels) {
            continue;
        }
        const QString cached = cacheRoot + QLatin1String(s.dir) + u'/' + hash + QStringLiteral(".png");
        QImageReader reader(cached);
        if (reader.canRead() && reader.text(QStringLiteral("Thumb::MTime")) == mtime) {
            QImage image = reader.read();
            if (!image.isNull()) {
                return image;
            }
        }
    }

    // We make images ourselves; the rest (videos, PDFs) only if it already
    // exists.
    QImageReader reader(path);
    reader.setAutoTransform(true); // rotated photos (EXIF)
    if (!reader.canRead() || info.size() > maxImageBytes) {
        return {};
    }
    QSize full = reader.size();
    if (full.isValid() && (full.width() > size->pixels || full.height() > size->pixels)) {
        reader.setScaledSize(full.scaled(size->pixels, size->pixels, Qt::KeepAspectRatio));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    if (image.width() > size->pixels || image.height() > size->pixels) {
        image = image.scaled(size->pixels, size->pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // In the cache for everyone, with the data the spec asks for.
    const QString dir = cacheRoot + QLatin1String(size->dir);
    if (QDir().mkpath(dir)) {
        QFile::setPermissions(cacheRoot, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QSaveFile file(dir + u'/' + hash + QStringLiteral(".png"));
        if (file.open(QIODevice::WriteOnly)) {
            QImageWriter writer(&file, "png");
            static const QMimeDatabase mimes;
            writer.setText(QStringLiteral("Thumb::URI"), QString::fromLatin1(uri));
            writer.setText(QStringLiteral("Thumb::MTime"), mtime);
            writer.setText(QStringLiteral("Thumb::Size"), QString::number(info.size()));
            writer.setText(QStringLiteral("Thumb::Mimetype"), mimes.mimeTypeForFile(info).name());
            if (full.isValid()) {
                writer.setText(QStringLiteral("Thumb::Image::Width"), QString::number(full.width()));
                writer.setText(QStringLiteral("Thumb::Image::Height"), QString::number(full.height()));
            }
            writer.setText(QStringLiteral("Software"), QStringLiteral("vela-shell"));
            if (writer.write(image)) {
                file.commit();
            }
        }
    }
    return image;
}

// The job in the thread: delivers the image with a (queued) signal, so if Qt
// cancels the request and deletes the response nothing is left hanging.
class ThumbnailJob : public QObject, public QRunnable {
    Q_OBJECT

public:
    ThumbnailJob(QString path, int size)
        : m_path(std::move(path))
        , m_size(size)
    {
    }
    void run() override { emit done(thumbnailFor(m_path, m_size)); }

signals:
    void done(const QImage& image);

private:
    QString m_path;
    int m_size;
};

class ThumbnailResponse : public QQuickImageResponse {
public:
    explicit ThumbnailResponse(ThumbnailJob* job)
    {
        connect(job, &ThumbnailJob::done, this, [this](const QImage& image) {
            m_image = image;
            emit finished();
        }, Qt::QueuedConnection);
    }

    QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("no thumbnail") : QString();
    }

private:
    QImage m_image;
};

} // namespace

FileThumbnailProvider::FileThumbnailProvider()
{
    m_pool.setMaxThreadCount(2); // it must not steal CPU from the rest
}

QQuickImageResponse* FileThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize)
{
    // "<encoded path>/<version>": the version changes with the file.
    const QString path = QUrl::fromPercentEncoding(id.section(u'/', 0, 0).toUtf8());
    const int size = requestedSize.isValid() ? std::max(requestedSize.width(), requestedSize.height()) : 128;
    auto* job = new ThumbnailJob(path, size); // the pool deletes it when done
    auto* response = new ThumbnailResponse(job);
    m_pool.start(job);
    return response;
}

#include "filethumbnails.moc"
