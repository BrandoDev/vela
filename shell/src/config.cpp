#include "config.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

Config::Config(QObject* parent)
    : QObject(parent)
    , m_path(QSettings().fileName())
{
    // Chi scrive il file lo sostituisce (QSaveFile): si osserva anche la
    // cartella, e il file si riaggancia a ogni cambiamento.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        watch();
        reload();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    watch();
    reload();
}

void Config::watch()
{
    const QFileInfo info(m_path);
    if (info.exists() && !m_watcher.files().contains(m_path)) {
        m_watcher.addPath(m_path);
    }
    if (info.dir().exists() && !m_watcher.directories().contains(info.absolutePath())) {
        m_watcher.addPath(info.absolutePath());
    }
}

void Config::reload()
{
    QSettings settings;
    settings.sync(); // rilegge ciò che hanno scritto gli altri processi

    // VELA_WALLPAPER (per le prove) ha la precedenza sulla scelta salvata.
    QString wallpaper = qEnvironmentVariable("VELA_WALLPAPER");
    if (wallpaper.isEmpty()) {
        wallpaper = settings.value(QStringLiteral("appearance/wallpaper")).toString();
    }
    if (wallpaper.isEmpty() || (!wallpaper.startsWith(u':') && !QFileInfo::exists(wallpaper))) {
        wallpaper = defaultWallpaper();
    }
    if (wallpaper != m_wallpaper) {
        m_wallpaper = wallpaper;
        emit wallpaperChanged();
    }

    QColor accent(settings.value(QStringLiteral("appearance/accent")).toString());
    if (!accent.isValid()) {
        accent = defaultAccent();
    }
    if (accent != m_accent) {
        m_accent = accent;
        emit accentChanged();
    }

    const QString alignment = settings.value(QStringLiteral("taskbar/alignment")).toString() == QLatin1String("left")
        ? QStringLiteral("left")
        : QStringLiteral("center");
    const bool endTask = settings.value(QStringLiteral("taskbar/endTask"), true).toBool();
    const bool taskView = settings.value(QStringLiteral("taskbar/taskView"), true).toBool();
    if (alignment != m_taskbarAlignment || endTask != m_endTask || taskView != m_taskView) {
        m_taskbarAlignment = alignment;
        m_endTask = endTask;
        m_taskView = taskView;
        emit taskbarChanged();
    }

    auto mode = [&](const char* key) {
        return settings.value(QLatin1String(key)).toString() == QLatin1String("light") ? QStringLiteral("light")
                                                                                       : QStringLiteral("dark");
    };
    const QString shellTheme = mode("appearance/shellTheme");
    const QString appTheme = mode("appearance/appTheme");
    if (shellTheme != m_shellTheme || appTheme != m_appTheme) {
        m_shellTheme = shellTheme;
        m_appTheme = appTheme;
        emit themeChanged();
    }

    const bool dnd = settings.value(QStringLiteral("notifications/doNotDisturb"), false).toBool();
    if (dnd != m_doNotDisturb) {
        m_doNotDisturb = dnd;
        emit doNotDisturbChanged(dnd);
    }
}
