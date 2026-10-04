#include "appearance.h"

#include "iconprovider.h"
#include "mica.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

Appearance::Appearance(QObject* parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        watch();
        reload();
    });
    reload();
}

void Appearance::watch()
{
    // Chi scrive i file li sostituisce: si osservano anche le cartelle.
    const QString shellConfig = QSettings(QStringLiteral("Vela"), QStringLiteral("vela-shell")).fileName();
    for (const QString& path : { shellConfig, wallpaperTintCachePath() }) {
        const QFileInfo info(path);
        if (info.exists() && !m_watcher.files().contains(path)) {
            m_watcher.addPath(path);
        }
        if (info.dir().exists() && !m_watcher.directories().contains(info.absolutePath())) {
            m_watcher.addPath(info.absolutePath());
        }
    }
    if (!m_watching) {
        m_watching = true;
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    }
}

void Appearance::reload()
{
    QSettings shell(QStringLiteral("Vela"), QStringLiteral("vela-shell"));
    shell.sync();
    const bool light = shell.value(QStringLiteral("appearance/appTheme")).toString() == QLatin1String("light");
    QColor accent(shell.value(QStringLiteral("appearance/accent")).toString());
    if (!accent.isValid()) {
        accent = QColor(0x5b, 0x8c, 0xff);
    }
    const QColor tint = cachedWallpaperTint();
    const QColor mica = micaFromTint(tint, true, light);
    const QColor micaInactive = micaFromTint(tint, false, light);
    if (light == m_light && accent == m_accent && mica == m_mica && micaInactive == m_micaInactive) {
        return;
    }
    const bool first = !m_accent.isValid();
    m_light = light;
    m_accent = accent;
    m_mica = mica;
    m_micaInactive = micaInactive;
    applyIconTheme(light); // prima che il QML richieda le icone
    const QString mode = light ? QStringLiteral("l/") : QStringLiteral("d/");
    if (first) {
        m_iconMode = mode;
        return;
    }
    emit changed();
    QTimer::singleShot(300, this, [this, mode] {
        if (mode != m_iconMode) {
            m_iconMode = mode;
            emit iconModeChanged();
        }
    });
}
