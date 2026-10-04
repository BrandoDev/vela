#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QPixmapCache>
#include <QQuickImageProvider>
#include <QUrl>

#include <algorithm>

// Il tema di icone adatto alla modalità chiara o scura: la variante
// "-dark" (breeze-dark) sullo scuro, quella normale (breeze) sul chiaro,
// partendo dal tema scelto in KDE. Da chiamare all'avvio e quando la
// modalità cambia. Con la piattaforma di KDE le icone le dà KIconLoader,
// che colora quelle monocromatiche con la tavolozza dell'app: anche quella
// segue la modalità.
inline void applyIconTheme(bool light)
{
    QPalette palette = QGuiApplication::palette();
    const QColor text = light ? QColor(0x1b, 0x1b, 0x1b) : QColor(0xf3, 0xf3, 0xf6);
    const QColor background = light ? QColor(0xf3, 0xf3, 0xf3) : QColor(0x20, 0x20, 0x20);
    for (const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
        palette.setColor(group, QPalette::WindowText, text);
        palette.setColor(group, QPalette::Text, text);
        palette.setColor(group, QPalette::ButtonText, text);
        palette.setColor(group, QPalette::Window, background);
        palette.setColor(group, QPalette::Base, background);
        palette.setColor(group, QPalette::Button, background);
    }
    const bool changed = palette != QGuiApplication::palette();
    if (changed) {
        QGuiApplication::setPalette(palette);
    }

    static const QString base = [] {
        QString theme = QIcon::themeName();
        if (theme.isEmpty() || theme == QLatin1String("hicolor")) {
            theme = qEnvironmentVariable("VELA_ICON_THEME", QStringLiteral("breeze-dark"));
        }
        return theme;
    }();
    auto exists = [](const QString& name) {
        const QStringList paths = QIcon::themeSearchPaths();
        return std::any_of(paths.begin(), paths.end(),
            [&](const QString& dir) { return QFileInfo::exists(dir + u'/' + name + QStringLiteral("/index.theme")); });
    };
    QString plain = base;
    if (plain.endsWith(QLatin1String("-dark"))) {
        plain.chop(5);
    }
    const QString dark = plain + QStringLiteral("-dark");
    QString chosen = base;
    if (light && plain != base && exists(plain)) {
        chosen = plain;
    } else if (!light && dark != base && exists(dark)) {
        chosen = dark;
    }
    if (QIcon::themeName() != chosen) {
        QIcon::setThemeName(chosen);
    } else if (changed) {
        QIcon::setThemeName(chosen);
    }
    if (changed) {
        // Le icone già caricate hanno i colori di prima: si ricaricano.
        // KIconLoader (la piattaforma di KDE) svuota la sua cache quando
        // riceve il segnale che manda il modulo delle icone di KDE.
        QPixmapCache::clear();
        QDBusConnection::sessionBus().send(QDBusMessage::createSignal(QStringLiteral("/KIconLoader"),
            QStringLiteral("org.kde.KIconLoader"), QStringLiteral("iconChanged"))
            << 0);
    }
}

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
//
// Un prefisso davanti al nome: "l/" e "d/" dicono per quale modalità
// (chiara o scura) si chiede l'icona, così cambiando modalità l'indirizzo
// cambia e il QML la ricarica; "w/" la vuole bianca (sopra l'accento).
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
        QString name = QUrl::fromPercentEncoding(id.toUtf8());
        bool white = false;
        if (id.size() > 2 && id[1] == u'/' && (id[0] == u'l' || id[0] == u'd' || id[0] == u'w')) {
            white = id[0] == u'w';
            name = QUrl::fromPercentEncoding(id.mid(2).toUtf8());
        }

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
        if (white && !pixmap.isNull()) {
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), Qt::white);
        }
        if (size) {
            *size = pixmap.size();
        }
        return pixmap;
    }

private:
    int m_designSize;
};
