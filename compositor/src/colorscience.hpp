// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Il colore dello schermo (accessibility.cpp, docs/renderer.md §7.6): la
// Luce notturna (il bianco di un corpo nero e quanto resta di ogni canale)
// e le matrici dei filtri colore. Funzioni pure, provate in
// compositor/tests/colorscience.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace vela::color {

inline float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Il bianco di un corpo nero a `kelvin` (Tanner Helland), in sRGB 0-1.
inline void blackbody(double kelvin, double out[3])
{
    const double t = kelvin / 100.0;
    double r = 255.0;
    double g = 0.0;
    double b = 255.0;
    if (t <= 66.0) {
        g = 99.4708025861 * std::log(t) - 161.1195681661;
        b = t <= 19.0 ? 0.0 : 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
    } else {
        r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
        g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
    }
    out[0] = std::clamp(r, 0.0, 255.0) / 255.0;
    out[1] = std::clamp(g, 0.0, 255.0) / 255.0;
    out[2] = std::clamp(b, 0.0, 255.0) / 255.0;
}

// Quanto resta di rosso, verde e blu (in luce lineare) a quella
// temperatura, rispetto al bianco normale dello schermo (6500 K).
inline void nightGains(double kelvin, float out[3])
{
    double white[3];
    double warm[3];
    blackbody(6500.0, white);
    blackbody(kelvin, warm);
    for (int i = 0; i < 3; ++i) {
        const float reference = srgbToLinear(float(white[i]));
        out[i] = std::clamp(srgbToLinear(float(warm[i])) / std::max(reference, 1e-4f), 0.0f, 1.0f);
    }
}

using Matrix = std::array<float, 9>;

inline constexpr Matrix identity { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

inline Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix out {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += a[r * 3 + k] * b[k * 3 + c];
            }
            out[r * 3 + c] = sum;
        }
    }
    return out;
}

// Correzione dei daltonismi (daltonizzazione): ciò che chi ha quel
// daltonismo perde (la differenza dalla simulazione di Machado, Oliveira
// e Fernandes 2009, gravità piena) si sposta sui canali che vede.
inline Matrix daltonize(const Matrix& simulation, const Matrix& shift)
{
    Matrix lost {};
    for (int i = 0; i < 9; ++i) {
        lost[i] = identity[i] - simulation[i];
    }
    const Matrix moved = multiply(shift, lost);
    Matrix out {};
    for (int i = 0; i < 9; ++i) {
        out[i] = identity[i] + moved[i];
    }
    return out;
}

inline Matrix filterMatrix(const std::string& kind)
{
    // Rosso e verde persi finiscono su verde e blu; il blu perso sul rosso e sul verde.
    constexpr Matrix redGreenShift { 0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1 };
    constexpr Matrix blueShift { 1, 0, 0.7f, 0, 1, 0.7f, 0, 0, 0 };
    if (kind == "deuteranopia") {
        return daltonize({ 0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f,
                             0.968881f },
            redGreenShift);
    }
    if (kind == "protanopia") {
        return daltonize({ 0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f,
                             1.051998f },
            redGreenShift);
    }
    if (kind == "tritanopia") {
        return daltonize({ 1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f,
                             0.303900f },
            blueShift);
    }
    // Scala di grigi: la luminanza su tutti e tre i canali.
    return { 0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f };
}

// L'intensità di Windows (0-100) in gradi: da 6500 K (spenta) fino a 1700 K.
inline double nightKelvin(int strength)
{
    return 6500.0 - std::clamp(strength, 0, 100) / 100.0 * (6500.0 - 1700.0);
}

} // namespace vela::color
