#pragma once

// Il colore Mica delle finestre di Vela: lo stesso calcolo del compositor
// per la barra del titolo (compositor/src/decoration.cpp, micaColor), così
// il contenuto delle app di Vela (Impostazioni, Esplora) continua la barra
// senza stacchi. La tinta è il colore medio dello sfondo: la calcola la
// shell e la lascia anche in ~/.cache/vela/wallpaper-tint, da leggere al
// volo all'avvio delle app.

#include <QColor>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <algorithm>

inline QString wallpaperTintCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/vela/wallpaper-tint");
}

inline void saveWallpaperTint(const QColor& tint)
{
    const QString path = wallpaperTintCachePath();
    QDir().mkpath(path.section(u'/', 0, -2));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QByteArray::number(tint.red()) + ' ' + QByteArray::number(tint.green()) + ' '
            + QByteArray::number(tint.blue()) + '\n');
    }
}

// Non valido se la shell non l'ha ancora scritta.
inline QColor cachedWallpaperTint()
{
    QFile file(wallpaperTintCachePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QList<QByteArray> parts = file.readAll().trimmed().split(' ');
    if (parts.size() != 3) {
        return {};
    }
    return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
}

// Scuro: attiva base #202020 al 30% verso la tinta, inattiva #2b2b2b al
// 45%. Chiaro: #f3f3f3 e #f9f9f9, con la tinta portata nella fascia
// chiara. Come micaColor in compositor/src/decoration.cpp.
inline QColor micaFromTint(const QColor& tint, bool active, bool light = false)
{
    const float base = light ? (active ? 0.953f : 0.976f) : (active ? 0.125f : 0.169f);
    if (!tint.isValid()) {
        return QColor::fromRgbF(base, base, base);
    }
    const float weight = active ? 0.30f : 0.45f;
    const float t[3] = { float(tint.redF()), float(tint.greenF()), float(tint.blueF()) };
    const float luma = 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2];
    float out[3];
    for (int i = 0; i < 3; ++i) {
        const float desaturated = luma + (t[i] - luma) * 0.5f;
        const float safe = light
            ? std::clamp(1.0f - (1.0f - desaturated) * (0.10f / std::max(1.0f - luma, 0.02f)), 0.82f, 1.0f)
            : std::clamp(desaturated * (0.16f / std::max(luma, 0.02f)), 0.0f, 0.32f);
        out[i] = base + (safe - base) * weight;
    }
    return QColor::fromRgbF(out[0], out[1], out[2]);
}
