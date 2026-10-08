// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "appearance.h"

#include "defaults.h"
#include "iconprovider.h"
#include "mica.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

Appearance::Appearance(QObject* parent, Mode mode)
    : QObject(parent)
    , m_mode(mode)
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
    // Whoever writes the files replaces them: the directories are watched too.
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
    const QString key = m_mode == Mode::Shell ? QStringLiteral("appearance/shellTheme") : QStringLiteral("appearance/appTheme");
    const bool light = shell.value(key).toString() == QLatin1String("light");
    QColor accent(shell.value(QStringLiteral("appearance/accent")).toString());
    if (!accent.isValid()) {
        accent = vela::defaults::accent();
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
    if (first) {
        applyIconTheme(light); // before QML asks for the icons
        m_iconMode = iconModeFor(light);
        return;
    }
    switchIconMode(this, light);
    emit changed();
}

void Appearance::setIconMode(const QString& mode)
{
    if (mode != m_iconMode) {
        m_iconMode = mode;
        emit iconModeChanged();
    }
}
