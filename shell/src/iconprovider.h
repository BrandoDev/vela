#pragma once

#include <QIcon>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QUrl>

#include <algorithm>

// Rende disponibili le icone del tema di sistema al QML:
//   Image { source: "image://icon/" + encodeURIComponent("org.kde.dolphin") }
// Accetta sia nomi di icone del tema sia percorsi assoluti.
class IconProvider : public QQuickImageProvider {
public:
    IconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const int extent = requestedSize.isValid()
            ? std::max({ requestedSize.width(), requestedSize.height(), 16 })
            : 64;
        const QString name = QUrl::fromPercentEncoding(id.toUtf8());

        QIcon icon = name.startsWith(u'/') ? QIcon(name) : QIcon::fromTheme(name);
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }
        const QPixmap pixmap = icon.pixmap(QSize(extent, extent));
        if (size) {
            *size = pixmap.size();
        }
        return pixmap;
    }
};
