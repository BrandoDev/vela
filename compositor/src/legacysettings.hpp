// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// I nomi italiani che vela.conf ha avuto fino a ottobre 2026, e quelli
// inglesi che li hanno sostituiti. Chi legge il file (il compositor, le
// Impostazioni) converte al volo; il compositor all'avvio riscrive il file
// con i nomi nuovi (migrateSettings). Solo libreria standard: lo usano
// anche le Impostazioni, che sono in Qt.

#include <string>
#include <string_view>

namespace vela::legacy {

struct Rename {
    std::string_view from;
    std::string_view to;
};

inline constexpr Rename settingKeys[] = {
    { "spegni-schermo", "screen-off" },
    { "blocca", "lock-on-idle" },
    { "tastiera-layout", "keyboard-layout" },
    { "tastiera-variante", "keyboard-variant" },
    { "tastiera-opzioni", "keyboard-options" },
    { "tastiera-ritardo", "keyboard-repeat-delay" },
    { "tastiera-velocita", "keyboard-repeat-rate" },
    { "luce-notturna", "night-light" },
    { "luce-notturna-intensita", "night-light-strength" },
    { "luce-notturna-pianifica", "night-light-schedule" },
    { "luce-notturna-dalle", "night-light-from" },
    { "luce-notturna-alle", "night-light-to" },
    { "filtri-colore", "color-filters" },
    { "filtro-colore", "color-filter" },
    { "filtri-colore-scorciatoia", "color-filters-shortcut" },
    { "lente-incremento", "magnifier-step" },
    { "tasti-permanenti", "sticky-keys" },
    { "frequenza-variabile", "variable-refresh" },
    { "mouse-velocita", "mouse-speed" },
    { "mouse-precisione", "mouse-precision" },
    { "mouse-pulsante-principale", "mouse-primary-button" },
    { "mouse-righe", "mouse-scroll-lines" },
    { "touchpad-con-mouse", "touchpad-with-mouse" },
    { "touchpad-velocita", "touchpad-speed" },
    { "touchpad-tocco", "touchpad-tap" },
    { "touchpad-scorrimento-naturale", "touchpad-natural-scroll" },
    { "touchpad-tre-dita", "touchpad-three-fingers" },
    { "touchpad-quattro-dita", "touchpad-four-fingers" },
    { "lingua", "language" },
};

struct ValueRename {
    std::string_view key; // nome nuovo; vuoto: qualunque chiave
    std::string_view from;
    std::string_view to;
};

inline constexpr ValueRename settingValues[] = {
    { "", "sì", "yes" },
    { "", "si", "yes" },
    { "night-light-schedule", "tramonto", "sunset" },
    { "night-light-schedule", "ore", "hours" },
    { "variable-refresh", "giochi", "games" },
    { "variable-refresh", "sempre", "always" },
    { "color-filter", "grigi", "grayscale" },
    { "mouse-primary-button", "sinistro", "left" },
    { "mouse-primary-button", "destro", "right" },
};

// Una riga "chiave=valore" con i nomi di adesso. true se è cambiata.
inline bool modernize(std::string& key, std::string& value)
{
    bool changed = false;
    for (const Rename& r : settingKeys) {
        if (key == r.from) {
            key = std::string(r.to);
            changed = true;
            break;
        }
    }
    for (const ValueRename& r : settingValues) {
        if ((r.key.empty() || key == r.key) && value == r.from) {
            value = std::string(r.to);
            changed = true;
            break;
        }
    }
    return changed;
}

// Lo stesso per una riga intera del file (commenti e righe vuote restano).
inline bool modernizeLine(std::string& line)
{
    const size_t eq = line.find('=');
    if (line.empty() || line.front() == '#' || eq == std::string::npos) {
        return false;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    if (!modernize(key, value)) {
        return false;
    }
    line = key + "=" + value;
    return true;
}

} // namespace vela::legacy
