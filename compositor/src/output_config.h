// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_OUTPUT_CONFIG_H
#define VELA_OUTPUT_CONFIG_H

// La configurazione degli schermi che l'utente ha scelto (con un programma
// di wlr-output-management, o con le Impostazioni di Vela), ricordata tra
// una sessione e l'altra in ~/.config/vela/outputs.conf:
//
//   [Marca Modello Seriale]
//   enabled=yes
//   mode=2560x1440@180.000
//   scale=1.25
//   rotation=normal
//   position=0,0
//
// Ogni monitor è riconosciuto da marca, modello e numero di serie, non dal
// connettore: spostarlo su un'altra porta non gli fa perdere le impostazioni.
// Fino a ottobre 2026 il file era schermi.conf, in italiano: si legge
// ancora, e alla prima scrittura diventa outputs.conf.

#include <stdbool.h>

// Uno schermo come l'utente l'ha lasciato. transform è un
// wl_output_transform (0 normale ... 7 specchiato e ruotato di 270°).
struct vela_saved_output {
    bool enabled;
    int width; // 0: modalità non salvata
    int height;
    int refresh_mhz;
    float scale; // 0: non salvata
    int transform;
    bool has_position;
    int x, y;
};

// Il monitor `key` come l'ha lasciato l'utente; false se non è nel file.
bool vela_output_config_load(const char *key, struct vela_saved_output *out);

// Scrive la configurazione attuale di questi schermi, lasciando com'era
// quella degli altri monitor (scollegati adesso). Per uno schermo spento si
// scrive solo enabled=no: il resto resta quello di prima.
struct vela_output_config_entry {
    const char *key;
    bool enabled;
    int width, height, refresh_mhz;
    float scale;
    int transform;
    int x, y;
};
void vela_output_config_save(const struct vela_output_config_entry *entries, int count);

#endif
