// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Le impostazioni di Vela che non riguardano gli schermi, in
// ~/.config/vela/vela.conf: righe chiave=valore (# per i commenti). Le
// scrive l'app Impostazioni, che poi manda "reload-config" al compositor.
//
//   screen-off=10            minuti di inattività (0: mai)
//   lock-on-idle=yes         bloccare prima di spegnere
//   keyboard-layout=it,us    layout XKB (Win+Spazio passa al successivo)
//   keyboard-variant=,
//   keyboard-options=
//   keyboard-repeat-delay=400  ms prima che un tasto premuto si ripeta
//   keyboard-repeat-rate=30  ripetizioni al secondo
//   night-light=no           Luce notturna accesa (la cambiano anche le
//                            impostazioni rapide e la pianificazione)
//   night-light-strength=48  0-100
//   night-light-schedule=no|sunset|hours
//   night-light-from=21:00, night-light-to=07:00
//   color-filters=no         filtro colore acceso
//   color-filter=grayscale   grayscale, deuteranopia, protanopia, tritanopia
//   color-filters-shortcut=no  Win+Ctrl+C accende e spegne
//   magnifier-step=100       di quanto ingrandisce Win+più (percento)
//   sticky-keys=no           Maiusc, Ctrl, Alt e Win restano premuti
//   tearing=yes              i giochi a schermo intero che lo chiedono
//   variable-refresh=games   VRR: no, games (app a schermo intero), always
//   mouse-speed=10           1-20, come il cursore di Windows
//   mouse-precision=yes      accelerazione del puntatore
//   mouse-primary-button=left|right
//   mouse-scroll-lines=3     righe per scatto della rotellina
//   touchpad=yes, touchpad-with-mouse=yes (resta acceso anche con un mouse)
//   touchpad-speed=10, touchpad-tap=yes, touchpad-natural-scroll=yes
//   touchpad-three-fingers=app  gesti: app, desktop, no
//   touchpad-four-fingers=desktop
//   language=                it, en, o vuota (come il sistema)
//
// I nomi italiani di prima si leggono ancora (legacysettings.hpp).

#include <map>
#include <string>

namespace vela {

using Settings = std::map<std::string, std::string>;

Settings readSettings();

// Il valore della chiave, o `fallback` se manca.
std::string setting(const Settings& settings, const std::string& key, const std::string& fallback = {});
// "yes"/"1"/"true" (o `fallback` se manca).
bool settingFlag(const Settings& settings, const std::string& key, bool fallback);

// Cambia una chiave nel file lasciando com'è il resto (anche i commenti):
// per ciò che si accende anche fuori dalle Impostazioni (impostazioni
// rapide, scorciatoie).
void writeSetting(const std::string& key, const std::string& value);

// Riscrive vela.conf con i nomi inglesi delle chiavi e dei valori, se ha
// ancora quelli italiani di prima (legacysettings.hpp). All'avvio.
void migrateSettings();

} // namespace vela
