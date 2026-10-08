// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Luce notturna, filtri colore e ore del sole (color.h, sun.h).

extern "C" {
#include "color.h"
#include "sun.h"
}

#include <gtest/gtest.h>

TEST(Color, NightKelvinFromStrength)
{
    EXPECT_EQ(vela_night_kelvin(0), 6500.0);
    EXPECT_EQ(vela_night_kelvin(100), 1700.0);
    EXPECT_EQ(vela_night_kelvin(50), 4100.0);
    EXPECT_EQ(vela_night_kelvin(-10), 6500.0);
    EXPECT_EQ(vela_night_kelvin(500), 1700.0);
}

TEST(Color, NightGainsAreNeutralAt6500AndWarmBelow)
{
    float gains[3];
    vela_night_gains(6500.0, gains);
    for (float g : gains) {
        EXPECT_NEAR(g, 1.0f, 1e-3f);
    }
    float previousBlue = 1.0f;
    for (double kelvin = 6000.0; kelvin >= 1700.0; kelvin -= 500.0) {
        vela_night_gains(kelvin, gains);
        EXPECT_NEAR(gains[0], 1.0f, 1e-3f) << kelvin; // il rosso resta
        EXPECT_LE(gains[1], 1.0f);
        EXPECT_LT(gains[2], previousBlue) << kelvin; // il blu cala sempre
        previousBlue = gains[2];
    }
}

// Ogni filtro lascia il bianco bianco (righe che sommano a 1): cambia i
// colori, non la luminosità dello schermo.
TEST(Color, FiltersKeepWhiteWhite)
{
    for (const char* kind : { "grayscale", "deuteranopia", "protanopia", "tritanopia" }) {
        float m[9];
        vela_filter_matrix(kind, m);
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(m[row * 3] + m[row * 3 + 1] + m[row * 3 + 2], 1.0f, 1e-3f) << kind << " riga " << row;
        }
    }
    float gray[9];
    vela_filter_matrix("grayscale", gray);
    for (int row = 1; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            EXPECT_EQ(gray[row * 3 + column], gray[column]); // tre canali uguali
        }
    }
}

TEST(Color, MatrixMultiplyWithIdentity)
{
    float m[9];
    vela_filter_matrix("protanopia", m);
    float left[9];
    float right[9];
    vela_matrix_multiply(vela_identity, m, left);
    vela_matrix_multiply(m, vela_identity, right);
    for (int i = 0; i < 9; ++i) {
        EXPECT_EQ(left[i], m[i]) << i;
        EXPECT_EQ(right[i], m[i]) << i;
    }
}

TEST(Sun, ParseZoneCoordinates)
{
    double latitude = 0, longitude = 0;
    ASSERT_TRUE(vela_sun_parse_coordinates("+4154+01229", &latitude, &longitude)); // Europe/Rome
    EXPECT_NEAR(latitude, 41.9, 1e-9);
    EXPECT_NEAR(longitude, 12.4833333, 1e-6);
    ASSERT_TRUE(vela_sun_parse_coordinates("+404251-0740023", &latitude, &longitude)); // America/New_York
    EXPECT_NEAR(latitude, 40.714166, 1e-5);
    EXPECT_NEAR(longitude, -74.006388, 1e-5);
    ASSERT_TRUE(vela_sun_parse_coordinates("-3352+15113", &latitude, &longitude)); // Australia/Sydney
    EXPECT_LT(latitude, 0.0);
    EXPECT_FALSE(vela_sun_parse_coordinates("", &latitude, &longitude));
    EXPECT_FALSE(vela_sun_parse_coordinates("4154+01229", &latitude, &longitude));
    EXPECT_FALSE(vela_sun_parse_coordinates("+41", &latitude, &longitude));
}

namespace {

tm day(int yday, long offsetSeconds)
{
    tm t {};
    t.tm_yday = yday;
    t.tm_gmtoff = offsetSeconds;
    return t;
}

int minutes(int hours, int mins)
{
    return hours * 60 + mins;
}

} // namespace

// Roma il 21 giugno (ora legale): alba 5:35, tramonto 20:48 circa.
TEST(Sun, RomeSummerSolstice)
{
    const tm june21 = day(171, 2 * 3600);
    EXPECT_NEAR(vela_sun_time(true, &june21, 41.9, 12.48), minutes(5, 35), 6);
    EXPECT_NEAR(vela_sun_time(false, &june21, 41.9, 12.48), minutes(20, 48), 6);
}

// Roma il 21 dicembre (ora solare): alba 7:33, tramonto 16:42 circa.
TEST(Sun, RomeWinterSolstice)
{
    const tm december21 = day(354, 3600);
    EXPECT_NEAR(vela_sun_time(true, &december21, 41.9, 12.48), minutes(7, 33), 6);
    EXPECT_NEAR(vela_sun_time(false, &december21, 41.9, 12.48), minutes(16, 42), 6);
}

TEST(Sun, PolarDayAndNight)
{
    // Tromsø: d'estate il sole non tramonta, d'inverno non sorge.
    const tm summer = day(171, 2 * 3600);
    const tm winter = day(354, 3600);
    EXPECT_EQ(vela_sun_time(false, &summer, 69.65, 18.96), -1);
    EXPECT_EQ(vela_sun_time(true, &winter, 69.65, 18.96), -1);
}

TEST(Sun, ParseClockAndRanges)
{
    EXPECT_EQ(vela_parse_clock("21:30", -1), minutes(21, 30));
    EXPECT_EQ(vela_parse_clock("7:05", -1), minutes(7, 5));
    EXPECT_EQ(vela_parse_clock("24:00", -1), -1);
    EXPECT_EQ(vela_parse_clock("12:60", -1), -1);
    EXPECT_EQ(vela_parse_clock("sera", 42), 42);
    // Dalle 21 alle 7: attraverso la mezzanotte.
    EXPECT_TRUE(vela_in_range(minutes(23, 0), minutes(21, 0), minutes(7, 0)));
    EXPECT_TRUE(vela_in_range(minutes(6, 59), minutes(21, 0), minutes(7, 0)));
    EXPECT_FALSE(vela_in_range(minutes(7, 0), minutes(21, 0), minutes(7, 0)));
    EXPECT_FALSE(vela_in_range(minutes(12, 0), minutes(21, 0), minutes(7, 0)));
    // Dalle 13 alle 15: lo stesso giorno.
    EXPECT_TRUE(vela_in_range(minutes(14, 0), minutes(13, 0), minutes(15, 0)));
    EXPECT_FALSE(vela_in_range(minutes(15, 0), minutes(13, 0), minutes(15, 0)));
}
