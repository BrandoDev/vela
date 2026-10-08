// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_CONFIG_H
#define VELA_CONFIG_H

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
// I nomi italiani di prima si leggono ancora (legacy_names.h).

#include <stdbool.h>
#include <stddef.h>

// Il file letto: una voce per chiave (a parità di chiave vince l'ultima
// riga). Ogni voce è una sola allocazione, "chiave\0valore".
struct vela_config_entry {
    char *key;
    const char *value; // dentro la stessa allocazione di key
};

struct vela_config {
    struct vela_config_entry *entries;
    int count;
    int capacity;
};

// Legge vela.conf (un file che manca dà un elenco vuoto). Va liberato con
// vela_config_finish.
void vela_config_read(struct vela_config *config);
void vela_config_finish(struct vela_config *config);

// Il valore della chiave, o `fallback` se manca.
const char *vela_config_get(const struct vela_config *config, const char *key, const char *fallback);
// "yes"/"1"/"true"; `fallback` se manca o è vuota.
bool vela_config_flag(const struct vela_config *config, const char *key, bool fallback);
// Un intero, o `fallback` se manca o è vuota.
int vela_config_int(const struct vela_config *config, const char *key, int fallback);

// Cambia una chiave nel file lasciando com'è il resto (anche i commenti):
// per ciò che si accende anche fuori dalle Impostazioni (impostazioni
// rapide, scorciatoie).
void vela_config_write(const char *key, const char *value);

// Riscrive vela.conf con i nomi inglesi delle chiavi e dei valori, se ha
// ancora quelli italiani di prima. All'avvio.
void vela_config_migrate(void);

// $XDG_CONFIG_HOME/vela/<name> (o ~/.config/vela/<name>); name NULL: la
// cartella. false se non si sa dove sia la cartella o non ci sta in `size`.
bool vela_config_path(const char *name, char *out, size_t size);

#endif
