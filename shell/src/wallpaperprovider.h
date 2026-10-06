// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QQuickImageProvider>
#include <QSvgRenderer>
#include <QUrl>

// Lo sfondo, già ritagliato alla dimensione esatta dello schermo:
//   Image { source: "image://wallpaper/" + encodeURIComponent(percorso) }
// L'immagine riempie lo schermo tagliando i bordi in eccesso (come "Riempi"
// di Windows). Un SVG viene disegnato direttamente a quella dimensione, così
// in memoria c'è solo ciò che si vede.
class WallpaperProvider : public QQuickImageProvider {
public:
    WallpaperProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const QString path = QUrl::fromPercentEncoding(id.toUtf8());
        // Mentre il compositor configura la finestra una delle due misure
        // può valere zero per un attimo: niente da disegnare.
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
    // La parte centrale di `source` con le proporzioni di `target`.
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
        // Dove il disegno non arriva (bordi): il suo blu più scuro, come
        // Theme.desktop, invece del nero.
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
            // Si decodifica già ritagliata e ridotta: niente foto da 50
            // megapixel intere in memoria.
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
