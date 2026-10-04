#pragma once

#include <QIcon>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QUrl>

#include <algorithm>

// Rende disponibili le icone del tema di sistema al QML:
//   Image { source: "image://icon/" + encodeURIComponent("org.kde.dolphin")
//           sourceSize: Qt.size(width, height) }
// Accetta sia nomi di icone del tema sia percorsi assoluti. La sourceSize è
// la misura logica a cui si mostra: Qt la moltiplica per la scala dello
// schermo e qui si disegna esattamente a quei pixel.
//
// Due modi: "icon" usa il disegno del tema per quella misura (le icone dei
// comandi, che piccole sono monocromatiche e nitide); "fileicon" usa sempre
// il disegno colorato da almeno `designSize` pixel, ridotto con cura (file,
// cartelle e luoghi, colorati anche piccoli come in Windows).
class IconProvider : public QQuickImageProvider {
public:
    explicit IconProvider(int designSize = 0)
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
        , m_designSize(designSize)
    {
    }

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const int extent = requestedSize.isValid()
            ? std::max({ requestedSize.width(), requestedSize.height(), 8 })
            : 64;
        const QString name = QUrl::fromPercentEncoding(id.toUtf8());

        QIcon icon = name.startsWith(u'/') ? QIcon(name) : QIcon::fromTheme(name);
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }
        // Esattamente ai pixel richiesti: Qt ha già moltiplicato la
        // sourceSize (misura logica dell'icona) per la scala dello schermo.
        // Senza il rapporto 1, QIcon moltiplicherebbe di nuovo per la scala
        // intera dell'uscita e l'icona, rimpicciolita sullo schermo, verrebbe
        // sfocata (soprattutto a scale frazionarie come 125%).
        QPixmap pixmap;
        if (extent < m_designSize) {
            pixmap = icon.pixmap(QSize(m_designSize, m_designSize), 1.0)
                         .scaled(extent, extent, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } else {
            pixmap = icon.pixmap(QSize(extent, extent), 1.0);
        }
        if (size) {
            *size = pixmap.size();
        }
        return pixmap;
    }

private:
    int m_designSize;
};
