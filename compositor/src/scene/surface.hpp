#pragma once

// Lo stato di Vela legato a ogni wlr_surface: su quali schermi si vede, e
// quanto. Da qui partono enter/leave, la scala preferita e lo schermo che
// scandisce i frame callback della superficie (docs/renderer.md §4.5, §3.6).

#include "wlr.hpp"

#include <vector>

namespace vela::scene {

struct SurfaceState {
    wlr_surface* surface;

    struct OnOutput {
        wlr_output* output;
        int64_t overlap; // pixel della superficie che cadono sullo schermo
        int64_t visible; // di questi, quanti non sono coperti
    };
    std::vector<OnOutput> outputs;
    // Lo schermo che mostra la parte maggiore: da lui la scala preferita.
    wlr_output* primary = nullptr;
    // Lo schermo che ne mostra la parte visibile maggiore: da lui frame
    // callback e presentazione. Nessuno: la superficie è coperta o nascosta
    // e l'app può smettere di disegnare.
    wlr_output* pacing = nullptr;
};

SurfaceState* surfaceState(wlr_surface* surface);

// Da OutputFrame, a ogni frame disegnato: la superficie occupa `overlap`
// pixel dello schermo, `visible` non coperti.
void reportSurface(wlr_surface* surface, wlr_output* output, int64_t overlap, int64_t visible);
// Non è più su quello schermo.
void forgetSurface(wlr_surface* surface, wlr_output* output);

} // namespace vela::scene
