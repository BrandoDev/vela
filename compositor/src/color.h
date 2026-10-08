// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_COLOR_H
#define VELA_COLOR_H

// Il colore dello schermo (a11y.c, docs/renderer.md §7.6): la Luce notturna
// (il bianco di un corpo nero e quanto resta di ogni canale) e le matrici
// dei filtri colore, 3x3 per righe in spazio lineare. Funzioni pure,
// provate in compositor/tests/color_sun_test.cpp.

float vela_srgb_to_linear(float c);

// Il bianco di un corpo nero a `kelvin` (Tanner Helland), in sRGB 0-1.
void vela_blackbody(double kelvin, double out[3]);

// Quanto resta di rosso, verde e blu (in luce lineare) a quella
// temperatura, rispetto al bianco normale dello schermo (6500 K).
void vela_night_gains(double kelvin, float out[3]);

// L'intensità di Windows (0-100) in gradi: da 6500 K (spenta) fino a 1700 K.
double vela_night_kelvin(int strength);

extern const float vela_identity[9];

void vela_matrix_multiply(const float a[9], const float b[9], float out[9]);

// Il filtro colore per nome: grayscale (anche per nomi sconosciuti),
// deuteranopia, protanopia, tritanopia.
void vela_filter_matrix(const char *kind, float out[9]);

#endif
