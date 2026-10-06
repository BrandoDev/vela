// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// La configurazione degli schermi che l'utente ha scelto (con un programma
// di wlr-output-management, e più avanti con le Impostazioni di Vela),
// ricordata tra una sessione e l'altra in ~/.config/vela/outputs.conf.
// Ogni monitor è riconosciuto da marca, modello e numero di serie, non dal
// connettore: spostarlo su un'altra porta non gli fa perdere le impostazioni.

#include "wlr.hpp"

#include <optional>
#include <string>
#include <vector>

namespace vela {

struct SavedOutput {
    bool enabled = true;
    int width = 0; // 0: modalità non salvata
    int height = 0;
    int refreshMhz = 0;
    float scale = 0.0f; // 0: non salvata
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    bool hasPosition = false;
    int x = 0;
    int y = 0;
};

// Marca, modello e numero di serie; il nome del connettore se mancano.
std::string outputKey(const wlr_output* output);

std::optional<SavedOutput> loadSavedOutput(const wlr_output* output);

// Scrive la configurazione attuale di questi schermi, lasciando quella degli
// altri monitor (scollegati adesso) com'era.
struct CurrentOutput {
    const wlr_output* output;
    bool enabled;
    int x;
    int y;
};
void saveOutputs(const std::vector<CurrentOutput>& outputs);

// La modalità dello schermo più vicina a quella salvata, nullptr se non c'è.
wlr_output_mode* findMode(wlr_output* output, int width, int height, int refreshMhz);

} // namespace vela
