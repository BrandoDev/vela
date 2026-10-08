// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QQuickImageProvider>
#include <QSvgRenderer>
#include <QUrl>

// The wallpaper, already cropped to the output's exact size:
//   Image { source: "image://wallpaper/" + encodeURIComponent(path) }
// The image fills the output cropping the excess edges (like Windows' "Fill").
// An SVG is drawn directly at that size, so only what's seen is in memory.
class WallpaperProvider : public QQuickImageProvider {
public:
    WallpaperProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const QString path = QUrl::fromPercentEncoding(id.toUtf8());
        // While the compositor configures the window one of the two sizes can
        // be zero for a moment: nothing to draw.
        if (requestedSize.isValid() && requestedSize.isEmpty()) {
            if (size) {
                *size = {};
            }
            return {};
        }
        const QSize target = requestedSize.isValid() ? requestedSize : QSize(1920, 1080);
        QImage image = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
                || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive)
            ? renderSvg(path, target)
            : loadRaster(path, target);
        if (size) {
            *size = image.size();
        }
        return image;
    }

private:
    // The central part of `source` with `target`'s proportions.
    static QRectF cropToFill(const QRectF& source, const QSize& target)
    {
        const qreal aspect = qreal(target.width()) / target.height();
        QRectF crop = source;
        if (source.width() / source.height() > aspect) {
            crop.setWidth(source.height() * aspect);
        } else {
            crop.setHeight(source.width() / aspect);
        }
        crop.moveCenter(source.center());
        return crop;
    }

    static QImage renderSvg(const QString& path, const QSize& target)
    {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) {
            qWarning("vela-shell: invalid background: %s", qPrintable(path));
            return {};
        }
        renderer.setViewBox(cropToFill(renderer.viewBoxF(), target));
        renderer.setAspectRatioMode(Qt::IgnoreAspectRatio);
        QImage image(target, QImage::Format_RGB32);
        // Where the drawing doesn't reach (edges): its darkest blue, like
        // Theme.desktop, instead of black.
        image.fill(QColor(0x06, 0x18, 0x2d));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        return image;
    }

    static QImage loadRaster(const QString& path, const QSize& target)
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize original = reader.size();
        if (original.isValid()) {
            // Decoded already cropped and reduced: no whole 50-megapixel
            // photos in memory.
            const QRect crop = cropToFill(QRectF(QPointF(0, 0), QSizeF(original)), target).toRect();
            reader.setClipRect(crop);
            reader.setScaledSize(target);
        }
        QImage image = reader.read();
        if (image.isNull()) {
            qWarning("vela-shell: unreadable background: %s (%s)", qPrintable(path),
                qPrintable(reader.errorString()));
        }
        return image;
    }
};
