// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SUN_H
#define VELA_SUN_H

// Le ore del sole per la Luce notturna "dal tramonto all'alba" (a11y.c),
// senza rete: le coordinate vengono dal fuso orario. Funzioni pure, provate
// in compositor/tests/color_sun_test.cpp.

#include <stdbool.h>
#include <time.h>

// Le coordinate di zone1970.tab: "+4154+01229" (Roma) o "+404251-0740023"
// (New York), cioè ±GGPP[SS] di latitudine e ±GGGPP[SS] di longitudine.
bool vela_sun_parse_coordinates(const char *text, double *latitude, double *longitude);

// L'ora locale (minuti dalla mezzanotte) dell'alba o del tramonto di oggi,
// con l'algoritmo dell'"Almanac for Computers" (US Naval Observatory).
// -1: il sole quel giorno non sorge o non tramonta.
int vela_sun_time(bool sunrise, const struct tm *today, double latitude, double longitude);

// "21:30" -> minuti dalla mezzanotte, `fallback` se non è un'ora.
int vela_parse_clock(const char *text, int fallback);

// `now` cade in [from, to), anche attraverso la mezzanotte.
bool vela_in_range(int now, int from, int to);

#endif
