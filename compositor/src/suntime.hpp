// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Le ore del sole per la Luce notturna "dal tramonto all'alba"
// (accessibility.cpp), senza rete: le coordinate vengono dal fuso orario.
// Funzioni pure, provate in compositor/tests/suntime.cpp.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace vela::sun {

// Le coordinate di zone1970.tab: "+4154+01229" (Roma) o "+404251-0740023"
// (New York), cioè ±GGPP[SS] di latitudine e ±GGGPP[SS] di longitudine.
inline bool parseCoordinates(const std::string& text, double& latitude, double& longitude)
{
    // ±GGPP o ±GGPPSS, latitudine e longitudine di seguito.
    const auto parse = [](const std::string& field, size_t& pos, int degreeDigits, double& value) {
        if (pos >= field.size() || (field[pos] != '+' && field[pos] != '-')) {
            return false;
        }
        const double sign = field[pos] == '-' ? -1.0 : 1.0;
        size_t end = pos + 1;
        while (end < field.size() && std::isdigit(static_cast<unsigned char>(field[end]))) {
            ++end;
        }
        const std::string digits = field.substr(pos + 1, end - pos - 1);
        if (int(digits.size()) < degreeDigits + 2) {
            return false;
        }
        const double degrees = std::stod(digits.substr(0, size_t(degreeDigits)));
        const double minutes = std::stod(digits.substr(size_t(degreeDigits), 2));
        const double seconds = int(digits.size()) >= degreeDigits + 4 ? std::stod(digits.substr(size_t(degreeDigits) + 2, 2)) : 0.0;
        value = sign * (degrees + minutes / 60.0 + seconds / 3600.0);
        pos = end;
        return true;
    };
    size_t pos = 0;
    return parse(text, pos, 2, latitude) && parse(text, pos, 3, longitude);
}

// L'ora locale (minuti dalla mezzanotte) dell'alba o del tramonto di oggi,
// con l'algoritmo dell'"Almanac for Computers" (US Naval Observatory).
// -1: il sole quel giorno non sorge o non tramonta.
inline int sunTime(bool sunrise, const tm& today, double latitude, double longitude)
{
    constexpr double pi = 3.14159265358979323846;
    auto rad = [](double d) { return d * pi / 180.0; };
    auto deg = [](double r) { return r * 180.0 / pi; };
    auto wrap = [](double v, double range) { return v - range * std::floor(v / range); };

    const double day = today.tm_yday + 1;
    const double lngHour = longitude / 15.0;
    const double t = day + ((sunrise ? 6.0 : 18.0) - lngHour) / 24.0;
    const double anomaly = 0.9856 * t - 3.289;
    const double trueLong = wrap(anomaly + 1.916 * std::sin(rad(anomaly)) + 0.020 * std::sin(rad(2 * anomaly)) + 282.634, 360.0);
    double ascension = wrap(deg(std::atan(0.91764 * std::tan(rad(trueLong)))), 360.0);
    ascension += std::floor(trueLong / 90.0) * 90.0 - std::floor(ascension / 90.0) * 90.0;
    ascension /= 15.0;
    const double sinDec = 0.39782 * std::sin(rad(trueLong));
    const double cosDec = std::cos(std::asin(sinDec));
    const double cosHour = (std::cos(rad(90.833)) - sinDec * std::sin(rad(latitude))) / (cosDec * std::cos(rad(latitude)));
    if (cosHour > 1.0 || cosHour < -1.0) {
        return -1;
    }
    double hour = sunrise ? 360.0 - deg(std::acos(cosHour)) : deg(std::acos(cosHour));
    hour /= 15.0;
    const double local = hour + ascension - 0.06571 * t - 6.622;
    const double utc = wrap(local - lngHour, 24.0);
    const double offsetHours = double(today.tm_gmtoff) / 3600.0;
    return int(std::lround(wrap(utc + offsetHours, 24.0) * 60.0)) % (24 * 60);
}

// "21:30" -> minuti dalla mezzanotte.
inline int parseClock(const std::string& text, int fallback)
{
    int h = 0;
    int m = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
        return h * 60 + m;
    }
    return fallback;
}

inline bool inRange(int now, int from, int to)
{
    return from <= to ? (now >= from && now < to) : (now >= from || now < to);
}

} // namespace vela::sun
