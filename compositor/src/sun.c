// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sun.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>

// Il numero scritto nelle cifre [from, from + count) di `digits`.
static double digits_value(const char *digits, int from, int count)
{
    double value = 0.0;
    for (int i = from; i < from + count; ++i) {
        value = value * 10.0 + (digits[i] - '0');
    }
    return value;
}

// ±GG..PP[SS] a partire da *pos: `degree_digits` cifre di gradi, due di
// primi, due facoltative di secondi.
static bool parse_angle(const char *text, int *pos, int degree_digits, double *value)
{
    const char *start = text + *pos;
    if (*start != '+' && *start != '-') {
        return false;
    }
    double sign = *start == '-' ? -1.0 : 1.0;
    const char *digits = start + 1;
    int count = 0;
    while (isdigit((unsigned char)digits[count])) {
        ++count;
    }
    if (count < degree_digits + 2) {
        return false;
    }
    double degrees = digits_value(digits, 0, degree_digits);
    double minutes = digits_value(digits, degree_digits, 2);
    double seconds = count >= degree_digits + 4 ? digits_value(digits, degree_digits + 2, 2) : 0.0;
    *value = sign * (degrees + minutes / 60.0 + seconds / 3600.0);
    *pos += 1 + count;
    return true;
}

bool vela_sun_parse_coordinates(const char *text, double *latitude, double *longitude)
{
    int pos = 0;
    return parse_angle(text, &pos, 2, latitude) && parse_angle(text, &pos, 3, longitude);
}

static double rad(double d)
{
    return d * M_PI / 180.0;
}

static double deg(double r)
{
    return r * 180.0 / M_PI;
}

static double wrap(double v, double range)
{
    return v - range * floor(v / range);
}

int vela_sun_time(bool sunrise, const struct tm *today, double latitude, double longitude)
{
    double day = today->tm_yday + 1;
    double lng_hour = longitude / 15.0;
    double t = day + ((sunrise ? 6.0 : 18.0) - lng_hour) / 24.0;
    double anomaly = 0.9856 * t - 3.289;
    double true_long = wrap(anomaly + 1.916 * sin(rad(anomaly)) + 0.020 * sin(rad(2 * anomaly)) + 282.634, 360.0);
    double ascension = wrap(deg(atan(0.91764 * tan(rad(true_long)))), 360.0);
    ascension += floor(true_long / 90.0) * 90.0 - floor(ascension / 90.0) * 90.0;
    ascension /= 15.0;
    double sin_dec = 0.39782 * sin(rad(true_long));
    double cos_dec = cos(asin(sin_dec));
    double cos_hour = (cos(rad(90.833)) - sin_dec * sin(rad(latitude))) / (cos_dec * cos(rad(latitude)));
    if (cos_hour > 1.0 || cos_hour < -1.0) {
        return -1;
    }
    double hour = sunrise ? 360.0 - deg(acos(cos_hour)) : deg(acos(cos_hour));
    hour /= 15.0;
    double local = hour + ascension - 0.06571 * t - 6.622;
    double utc = wrap(local - lng_hour, 24.0);
    double offset_hours = (double)today->tm_gmtoff / 3600.0;
    return (int)(lround(wrap(utc + offset_hours, 24.0) * 60.0) % (24 * 60));
}

int vela_parse_clock(const char *text, int fallback)
{
    int h = 0;
    int m = 0;
    if (sscanf(text, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
        return h * 60 + m;
    }
    return fallback;
}

bool vela_in_range(int now, int from, int to)
{
    return from <= to ? (now >= from && now < to) : (now >= from || now < to);
}
