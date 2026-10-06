// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// La geometria della nitidezza (docs/renderer.md §3): la scala predefinita
// dai DPI, VELA_SCALE, e come si sceglie la dimensione logica di una finestra
// perché il suo buffer copra esattamente i pixel dell'area. Funzioni pure,
// provate in compositor/tests/geometry.cpp; le usa output.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace vela::geometry {

// La scala predefinita, come fa Windows (§3.8): dai DPI dello schermo, a
// passi del 25%, tra 100% e 300%. I pannelli dei portatili si guardano più da
// vicino: riferimento 105,6 DPI invece di 96. Misure assurde (proiettori, TV,
// adattatori che inventano l'EDID: diagonale fuori da 8"–100", o proporzioni
// diverse da quelle dei pixel) danno 100%, e dpi 0.
inline float scaleForDpi(double physWidthMm, double physHeightMm, int width, int height, bool internal, double& dpi)
{
    dpi = 0.0;
    if (physWidthMm <= 0.0 || physHeightMm <= 0.0 || width <= 0 || height <= 0) {
        return 1.0f;
    }
    const double diagonalMm = std::hypot(physWidthMm, physHeightMm);
    const double aspectError = std::abs((physWidthMm / physHeightMm) / (double(width) / height) - 1.0);
    if (diagonalMm < 8 * 25.4 || diagonalMm > 100 * 25.4 || aspectError > 0.1) {
        return 1.0f;
    }
    dpi = std::hypot(width, height) / (diagonalMm / 25.4);
    const double reference = internal ? 105.6 : 96.0;
    return float(std::clamp(std::round(dpi / reference * 4.0) / 4.0, 1.0, 3.0));
}

// Un pannello interno (portatile, tablet) dal nome del connettore.
inline bool isInternalPanel(const std::string& name)
{
    return name.starts_with("eDP") || name.starts_with("LVDS") || name.starts_with("DSI");
}

// La scala chiesta con VELA_SCALE per lo schermo `name`: "1.25" per tutti,
// oppure "DP-1=1.5,HDMI-A-1=1". 0 se non c'è.
inline float requestedScale(const char* value, const std::string& name)
{
    if (!value || !*value) {
        return 0.0f;
    }
    const std::string list = value;
    if (list.find('=') == std::string::npos) {
        return std::strtof(value, nullptr);
    }
    size_t start = 0;
    while (start < list.size()) {
        const size_t end = std::min(list.find(',', start), list.size());
        const std::string item = list.substr(start, end - start);
        const size_t eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == name) {
            return std::strtof(item.c_str() + eq + 1, nullptr);
        }
        start = end + 1;
    }
    return 0.0f;
}

// I pixel del buffer che un client disegna per `logical` unità a quella
// scala (fractional-scale-v1, in 120esimi, arrotondamento a metà).
inline int64_t bufferPixels(int64_t logical, double scale)
{
    const int64_t scale120 = std::lround(scale * 120.0);
    return (logical * scale120 + 60) / 120;
}

struct Axis {
    double offset; // posizione logica rispetto all'origine dello schermo
    int size; // dimensione logica
};

// Un asse di una finestra che deve occupare i pixel [start, start + size)
// di uno schermo largo `screen` pixel (§3.5): la dimensione logica il cui
// buffer è esattamente `size` pixel. Se non esiste (a 150% 2560 pixel
// sarebbero 1706,67 unità), il pixel in più sborda fuori dallo schermo dal
// lato di un bordo libero (senza un altro schermo accanto: openBefore e
// openAfter); altrimenti si resta un pixel dentro.
inline Axis placeAxis(int start, int size, int screen, double scale, bool openBefore, bool openAfter)
{
    const bool touchesStart = start <= 0 && openBefore;
    const bool touchesEnd = start + size >= screen && openAfter;
    int exact = 0;
    int above = 0; // il più piccolo che sfora
    int below = 1; // il più grande che resta dentro
    for (int64_t w = int64_t(std::floor(size / scale)) - 1; w <= int64_t(std::ceil(size / scale)) + 1; ++w) {
        if (w <= 0) {
            continue;
        }
        const int64_t pixels = bufferPixels(w, scale);
        if (pixels == size) {
            exact = int(w);
        } else if (pixels > size && !above) {
            above = int(w);
        } else if (pixels < size) {
            below = int(w);
        }
    }
    if (exact) {
        return { start / scale, exact };
    }
    if ((touchesStart || touchesEnd) && above) {
        // Si sfora dal lato dello schermo: oltre la fine, o prima
        // dell'inizio se è lì il bordo libero.
        const int64_t excess = bufferPixels(above, scale) - size;
        const double first = touchesEnd ? start : double(start - excess);
        return { first / scale, above };
    }
    return { start / scale, below };
}

// Un bordo dell'area libera dai pannelli, in pixel dello schermo: quelli che
// toccano i bordi dello schermo restano esattamente sui suoi pixel.
inline int physicalEdge(int logical, int fullStart, int fullEnd, int pixels, double scale)
{
    if (logical <= fullStart) {
        return 0;
    }
    if (logical >= fullEnd) {
        return pixels;
    }
    return int(std::lround((logical - fullStart) * scale));
}

// Un rettangolo in pixel dello schermo.
struct PixelBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Un rettangolo logico in pixel (§3.2): si arrotondano i bordi, non
// posizione e dimensione, così due rettangoli adiacenti restano adiacenti.
inline PixelBox edgesToPixels(double x, double y, double width, double height, double originX, double originY,
    double scale)
{
    const int x1 = int(std::lround((x - originX) * scale));
    const int y1 = int(std::lround((y - originY) * scale));
    const int x2 = int(std::lround((x + width - originX) * scale));
    const int y2 = int(std::lround((y + height - originY) * scale));
    return { x1, y1, x2 - x1, y2 - y1 };
}

// L'app ha disegnato alla scala dello schermo (§3.4): il buffer, a coordinate
// intere, è grande quanto la sua area fisica a meno dell'arrotondamento. Allora
// la superficie occupa esattamente i pixel del buffer.
inline bool bufferMatchesArea(double srcX, double srcY, double bufferWidth, double bufferHeight, double width,
    double height, double scale)
{
    const bool whole = srcX == std::floor(srcX) && srcY == std::floor(srcY) && bufferWidth == std::floor(bufferWidth)
        && bufferHeight == std::floor(bufferHeight);
    return whole && std::abs(bufferWidth - width * scale) < 1.0 && std::abs(bufferHeight - height * scale) < 1.0;
}

// Copia 1:1, senza ricampionamento (§3.3): ogni pixel del buffer cade
// esattamente su un pixel dello schermo. `swapped`: ruotato di 90°.
inline bool oneToOne(double srcX, double srcY, double srcWidth, double srcHeight, int boxWidth, int boxHeight,
    bool swapped)
{
    const double width = swapped ? srcHeight : srcWidth;
    const double height = swapped ? srcWidth : srcHeight;
    return srcX == std::floor(srcX) && srcY == std::floor(srcY) && width == double(boxWidth)
        && height == double(boxHeight);
}

} // namespace vela::geometry
