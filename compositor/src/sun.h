// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SUN_H
#define VELA_SUN_H

// Sun times for night light "from sunset to sunrise" (a11y.c), without
// network: the coordinates come from the time zone. Pure functions, tested in
// compositor/tests/color_sun_test.cpp.

#include <stdbool.h>
#include <time.h>

// Coordinates from zone1970.tab: "+4154+01229" (Rome) or "+404251-0740023"
// (New York), that is ±DDMM[SS] of latitude and ±DDDMM[SS] of longitude.
bool vela_sun_parse_coordinates(const char *text, double *latitude, double *longitude);

// Local time (minutes after midnight) of today's sunrise or sunset, with the
// "Almanac for Computers" algorithm (US Naval Observatory). -1: the sun
// doesn't rise or set that day.
int vela_sun_time(bool sunrise, const struct tm *today, double latitude, double longitude);

// "21:30" -> minutes after midnight, `fallback` if it isn't a time.
int vela_parse_clock(const char *text, int fallback);

// `now` falls in [from, to), also across midnight.
bool vela_in_range(int now, int from, int to);

#endif
