// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// La geometria della nitidezza (geometry.h, docs/renderer.md §3): la
// scala dai DPI, VELA_SCALE, il posizionamento a pixel esatti di snap e
// massimizzazione, i bordi in pixel, la copia 1:1.

extern "C" {
#include "geometry.h"
}

#include <gtest/gtest.h>

#include <cmath>

namespace {

// Larghezza e altezza fisiche (mm) di uno schermo da `inches` pollici.
void physicalSize(double inches, int width, int height, double& widthMm, double& heightMm)
{
    const double diagonal = std::hypot(width, height);
    widthMm = inches * 25.4 * width / diagonal;
    heightMm = inches * 25.4 * height / diagonal;
}

} // namespace

// La tabella del §3.8.
TEST(Geometry, DefaultScaleFromDpi)
{
    struct Case {
        const char* name;
        double inches;
        int width, height;
        float scale;
    };
    for (const Case& c : {
             Case { "DP-1", 24, 1920, 1080, 1.0f },
             Case { "DP-1", 27, 2560, 1440, 1.25f },
             Case { "DP-1", 32, 2560, 1440, 1.0f },
             Case { "DP-1", 34, 3440, 1440, 1.25f },
             Case { "DP-1", 27, 3840, 2160, 1.75f },
             Case { "HDMI-A-1", 32, 3840, 2160, 1.5f },
             Case { "eDP-1", 15.6, 1920, 1080, 1.25f },
             Case { "eDP-1", 14, 1920, 1200, 1.5f },
             Case { "eDP-1", 16, 2560, 1600, 1.75f },
             Case { "eDP-1", 14, 2880, 1800, 2.25f },
         }) {
        double widthMm = 0, heightMm = 0, dpi = 0;
        physicalSize(c.inches, c.width, c.height, widthMm, heightMm);
        EXPECT_EQ(vela_scale_for_dpi(widthMm, heightMm, c.width, c.height, vela_is_internal_panel(c.name), &dpi), c.scale)
            << c.inches << "\" " << c.width << "x" << c.height;
        EXPECT_GT(dpi, 80.0);
    }
}

TEST(Geometry, AbsurdPhysicalSizesFallBackTo100Percent)
{
    double dpi = -1;
    EXPECT_EQ(vela_scale_for_dpi(0, 0, 1920, 1080, false, &dpi), 1.0f); // proiettore: niente misure
    EXPECT_EQ(dpi, 0.0);
    double w = 0, h = 0;
    physicalSize(150, 3840, 2160, w, h); // 150": una TV o un EDID inventato
    EXPECT_EQ(vela_scale_for_dpi(w, h, 3840, 2160, false, &dpi), 1.0f);
    EXPECT_EQ(vela_scale_for_dpi(160, 90, 3840, 2160, false, &dpi), 1.0f); // 7": troppo piccolo
    EXPECT_EQ(vela_scale_for_dpi(600, 600, 3840, 2160, false, &dpi), 1.0f); // proporzioni sbagliate
    EXPECT_EQ(vela_scale_for_dpi(600, 340, 0, 0, false, &dpi), 1.0f);
}

TEST(Geometry, ScaleIsClampedAndInQuarterSteps)
{
    double w = 0, h = 0, dpi = 0;
    physicalSize(10, 7680, 4320, w, h); // 881 DPI
    EXPECT_EQ(vela_scale_for_dpi(w, h, 7680, 4320, false, &dpi), 3.0f);
    for (double inches = 11; inches < 60; inches += 0.7) {
        physicalSize(inches, 3840, 2160, w, h);
        const float scale = vela_scale_for_dpi(w, h, 3840, 2160, false, &dpi);
        EXPECT_EQ(scale * 4.0f, std::round(scale * 4.0f));
        EXPECT_GE(scale, 1.0f);
        EXPECT_LE(scale, 3.0f);
    }
}

TEST(Geometry, RequestedScale)
{
    EXPECT_EQ(vela_requested_scale(nullptr, "DP-1"), 0.0f);
    EXPECT_EQ(vela_requested_scale("", "DP-1"), 0.0f);
    EXPECT_EQ(vela_requested_scale("1.25", "DP-1"), 1.25f);
    EXPECT_EQ(vela_requested_scale("DP-1=1.5,HDMI-A-1=1", "DP-1"), 1.5f);
    EXPECT_EQ(vela_requested_scale("DP-1=1.5,HDMI-A-1=1", "HDMI-A-1"), 1.0f);
    EXPECT_EQ(vela_requested_scale("DP-1=1.5,HDMI-A-1=1", "DP-2"), 0.0f);
    EXPECT_EQ(vela_requested_scale("DP-10=2,DP-1=1.75", "DP-1"), 1.75f); // nomi che iniziano uguali
}

TEST(Geometry, BufferPixelsLikeFractionalScale)
{
    EXPECT_EQ(vela_buffer_pixels(853, 1.5), 1280); // metà di 2560 a 150%
    EXPECT_EQ(vela_buffer_pixels(1707, 1.5), 2561); // 2560 non si può: un pixel in più
    EXPECT_EQ(vela_buffer_pixels(1706, 1.5), 2559);
    EXPECT_EQ(vela_buffer_pixels(1024, 1.25), 1280);
    EXPECT_EQ(vela_buffer_pixels(100, 1.0), 100);
}

// Per ogni scala, schermo e divisione: il buffer copre esattamente i pixel
// dell'area; se è impossibile, il pixel in più va solo oltre un bordo libero
// dello schermo; altrimenti si resta dentro.
TEST(Geometry, PlacementCoversThePixelsExactly)
{
    const double scales[] = { 1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 2.75, 3.0 };
    const int screens[] = { 1280, 1366, 1440, 1920, 2560, 2880, 3440, 3840 };
    // Divisioni in dodicesimi, come i layout di snap: metà, terzi, quarti, 2/3.
    const int splits[][2] = { { 0, 12 }, { 0, 6 }, { 6, 12 }, { 0, 4 }, { 4, 8 }, { 8, 12 }, { 0, 3 }, { 3, 9 },
        { 9, 12 }, { 0, 8 }, { 8, 12 } };
    int exactCount = 0;
    int total = 0;
    for (double scale : scales) {
        for (int screen : screens) {
            for (const auto& split : splits) {
                const int start = screen * split[0] / 12;
                const int end = screen * split[1] / 12;
                const int size = end - start;
                for (bool open : { true, false }) {
                    const vela_axis axis = vela_place_axis(start, size, screen, scale, open, open);
                    const int64_t pixels = vela_buffer_pixels(axis.size, scale);
                    const double first = axis.offset * scale;
                    ++total;
                    if (pixels == size) {
                        ++exactCount;
                        EXPECT_DOUBLE_EQ(first, start) << scale << " " << screen << " " << split[0];
                        continue;
                    }
                    const bool touches = start == 0 || end == screen;
                    if (open && touches) {
                        // Copre l'area e sborda solo fuori dallo schermo.
                        EXPECT_GT(pixels, size);
                        // (first è start / scala × scala: a meno dell'arrotondamento)
                        EXPECT_LE(first, start + 1e-6);
                        EXPECT_GE(first + pixels, end - 1e-6);
                        EXPECT_TRUE(first < -1e-6 || first + pixels > screen + 1e-6);
                        EXPECT_LE(pixels - size, 2);
                    } else {
                        // Dentro l'area, un pixel al massimo più piccola.
                        EXPECT_LT(pixels, size);
                        EXPECT_GE(pixels, size - 2);
                        EXPECT_DOUBLE_EQ(first, start);
                    }
                }
            }
        }
    }
    // La maggior parte dei casi ha una dimensione esatta (il test serve).
    EXPECT_GT(exactCount, total / 2);
}

// Il caso del §3.5: a 150%, 2560 pixel sarebbero 1706,67 unità.
TEST(Geometry, MaximizedAt150PercentSpillsOnePixelOffScreen)
{
    const vela_axis open = vela_place_axis(0, 2560, 2560, 1.5, true, true);
    EXPECT_EQ(open.size, 1707);
    EXPECT_EQ(open.offset, 0.0);
    EXPECT_EQ(vela_buffer_pixels(open.size, 1.5), 2561);
    // Con uno schermo accanto a destra, il pixel in più va a sinistra.
    const vela_axis rightNeighbour = vela_place_axis(0, 2560, 2560, 1.5, true, false);
    EXPECT_EQ(rightNeighbour.size, 1707);
    EXPECT_DOUBLE_EQ(rightNeighbour.offset * 1.5, -1.0);
    // Con schermi da entrambi i lati si resta dentro.
    const vela_axis closed = vela_place_axis(0, 2560, 2560, 1.5, false, false);
    EXPECT_EQ(closed.size, 1706);
}

TEST(Geometry, SnappedHalvesNeverGapOrOverlap)
{
    for (double scale : { 1.0, 1.25, 1.5, 1.75, 2.0, 2.25 }) {
        for (int screen : { 1366, 1920, 2560, 3440, 3840 }) {
            const int middle = screen / 2;
            const vela_axis left = vela_place_axis(0, middle, screen, scale, true, true);
            const vela_axis right = vela_place_axis(middle, screen - middle, screen, scale, true, true);
            const int64_t leftEnd = int64_t(std::lround(left.offset * scale)) + vela_buffer_pixels(left.size, scale);
            const int64_t rightStart = int64_t(std::lround(right.offset * scale));
            EXPECT_LE(leftEnd, rightStart + 1) << scale << " " << screen; // nessuna sovrapposizione visibile
            EXPECT_GE(leftEnd, rightStart - 1) << scale << " " << screen; // nessun buco visibile
        }
    }
}

// Si arrotondano i bordi, non posizione e dimensione (§3.2): rettangoli
// adiacenti restano adiacenti a ogni scala e posizione.
TEST(Geometry, AdjacentRectanglesStayAdjacent)
{
    for (double scale : { 1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 3.0 }) {
        for (double x = 0.0; x < 40.0; x += 0.37) {
            const double width = 13.3;
            const vela_pixel_box a = vela_edges_to_pixels(x, 0, width, 10, 0, 0, scale);
            const vela_pixel_box b = vela_edges_to_pixels(x + width, 0, width, 10, 0, 0, scale);
            EXPECT_EQ(a.x + a.width, b.x) << scale << " " << x;
        }
    }
    const vela_pixel_box box = vela_edges_to_pixels(110, 50, 100, 40, 100, 50, 1.5);
    EXPECT_EQ(box.x, 15);
    EXPECT_EQ(box.y, 0);
    EXPECT_EQ(box.width, 150);
    EXPECT_EQ(box.height, 60);
}

TEST(Geometry, BufferMatchesArea)
{
    EXPECT_TRUE(vela_buffer_matches_area(0, 0, 1280, 720, 1024, 576, 1.25)); // disegnata alla scala dello schermo
    EXPECT_TRUE(vela_buffer_matches_area(0, 0, 1280, 721, 853.5, 480.5, 1.5)); // entro l'arrotondamento
    EXPECT_FALSE(vela_buffer_matches_area(0, 0, 1024, 576, 1024, 576, 1.25)); // scala intera: va ingrandita
    EXPECT_FALSE(vela_buffer_matches_area(0.5, 0, 1280, 720, 1024, 576, 1.25)); // sorgente non intera (viewport)
}

TEST(Geometry, OneToOneCopy)
{
    EXPECT_TRUE(vela_one_to_one(0, 0, 1280, 720, 1280, 720, false));
    EXPECT_TRUE(vela_one_to_one(10, 20, 720, 1280, 1280, 720, true)); // ruotata di 90°
    EXPECT_FALSE(vela_one_to_one(0, 0, 1280, 720, 1281, 720, false));
    EXPECT_FALSE(vela_one_to_one(0.5, 0, 1280, 720, 1280, 720, false));
}

TEST(Geometry, PhysicalEdges)
{
    // Un'area libera che tocca i bordi resta esattamente sui pixel dello schermo.
    EXPECT_EQ(vela_physical_edge(0, 0, 2048, 2560, 1.25), 0);
    EXPECT_EQ(vela_physical_edge(2048, 0, 2048, 2560, 1.25), 2560);
    EXPECT_EQ(vela_physical_edge(-5, 0, 2048, 2560, 1.25), 0);
    EXPECT_EQ(vela_physical_edge(1104, 0, 1152, 1440, 1.25), 1380); // sopra la taskbar da 48
    EXPECT_EQ(vela_physical_edge(100, 100, 2148, 2560, 1.25), 0); // schermo non nell'origine
}
