#pragma once

// Le impostazioni di Vela che non riguardano gli schermi, in
// ~/.config/vela/vela.conf: righe chiave=valore (# per i commenti). Le
// scrive l'app Impostazioni, che poi manda "reload-config" al compositor.
//
//   spegni-schermo=10        minuti di inattività (0: mai)
//   blocca=sì                bloccare prima di spegnere
//   tastiera-layout=it,us    layout XKB (Win+Spazio passa al successivo)
//   tastiera-variante=,
//   tastiera-opzioni=
//   tastiera-ritardo=400     ms prima che un tasto premuto si ripeta
//   tastiera-velocita=30     ripetizioni al secondo
//   luce-notturna=no         Luce notturna accesa (la cambiano anche le
//                            impostazioni rapide e la pianificazione)
//   luce-notturna-intensita=48             0-100
//   luce-notturna-pianifica=no|tramonto|ore
//   luce-notturna-dalle=21:00, luce-notturna-alle=07:00
//   filtri-colore=no         filtro colore acceso
//   filtro-colore=grigi      grigi, deuteranopia, protanopia, tritanopia
//   filtri-colore-scorciatoia=no           Win+Ctrl+C accende e spegne
//   lente-incremento=100     di quanto ingrandisce Win+più (percento)
//   tasti-permanenti=no      Maiusc, Ctrl, Alt e Win restano premuti
//   tearing=sì               i giochi a schermo intero che lo chiedono

#include <map>
#include <string>

namespace vela {

using Settings = std::map<std::string, std::string>;

Settings readSettings();

// Il valore della chiave, o `fallback` se manca.
std::string setting(const Settings& settings, const std::string& key, const std::string& fallback = {});
// "sì"/"si"/"1"/"true" (o `fallback` se manca).
bool settingFlag(const Settings& settings, const std::string& key, bool fallback);

// Cambia una chiave nel file lasciando com'è il resto (anche i commenti):
// per ciò che si accende anche fuori dalle Impostazioni (impostazioni
// rapide, scorciatoie).
void writeSetting(const std::string& key, const std::string& value);

} // namespace vela
