// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Motion design di Vela: curve e durate in un solo posto, così tutto il
// desktop si muove nello stesso modo. La shell QML usa gli stessi valori
// (vedi shell/qml/Theme.qml).

#include <algorithm>
#include <cmath>

namespace vela {

// Curva di Bézier cubica come quelle di CSS/QML: cubic-bezier(x1, y1, x2, y2).
class CubicBezier {
public:
    constexpr CubicBezier(double x1, double y1, double x2, double y2)
        : m_cx(3.0 * x1)
        , m_bx(3.0 * (x2 - x1) - 3.0 * x1)
        , m_ax(1.0 - 3.0 * x1 - (3.0 * (x2 - x1) - 3.0 * x1))
        , m_cy(3.0 * y1)
        , m_by(3.0 * (y2 - y1) - 3.0 * y1)
        , m_ay(1.0 - 3.0 * y1 - (3.0 * (y2 - y1) - 3.0 * y1))
    {
    }

    // x = tempo normalizzato [0, 1] -> progresso [0, 1]
    double operator()(double x) const
    {
        if (x <= 0.0) {
            return 0.0;
        }
        if (x >= 1.0) {
            return 1.0;
        }
        return sampleY(solveT(x));
    }

private:
    double sampleX(double t) const { return ((m_ax * t + m_bx) * t + m_cx) * t; }
    double sampleY(double t) const { return ((m_ay * t + m_by) * t + m_cy) * t; }
    double sampleDX(double t) const { return (3.0 * m_ax * t + 2.0 * m_bx) * t + m_cx; }

    // Trova t tale che sampleX(t) == x: Newton, con bisezione di riserva.
    double solveT(double x) const
    {
        double t = x;
        for (int i = 0; i < 8; ++i) {
            const double err = sampleX(t) - x;
            if (std::abs(err) < 1e-6) {
                return t;
            }
            const double d = sampleDX(t);
            if (std::abs(d) < 1e-6) {
                break;
            }
            t -= err / d;
        }
        double lo = 0.0;
        double hi = 1.0;
        t = x;
        for (int i = 0; i < 32; ++i) {
            const double v = sampleX(t);
            if (std::abs(v - x) < 1e-6) {
                break;
            }
            if (x > v) {
                lo = t;
            } else {
                hi = t;
            }
            t = (lo + hi) / 2.0;
        }
        return t;
    }

    double m_cx, m_bx, m_ax;
    double m_cy, m_by, m_ay;
};

// Un'interpolazione a durata fissa. Il tempo di partenza viene fissato al
// primo frame in cui viene letta: così il primo frame disegnato è sempre lo
// stato iniziale esatto, anche se tra la richiesta e il frame passa del tempo.
class Tween {
public:
    Tween() = default;
    Tween(double durationMs, const CubicBezier* curve)
        : m_durationMs(durationMs)
        , m_curve(curve)
    {
    }

    double progress(double nowMs)
    {
        if (m_startMs < 0.0) {
            m_startMs = nowMs;
        }
        const double x = std::clamp((nowMs - m_startMs) / m_durationMs, 0.0, 1.0);
        return m_curve ? (*m_curve)(x) : x;
    }

    bool finished(double nowMs) const
    {
        return m_startMs >= 0.0 && nowMs - m_startMs >= m_durationMs;
    }

private:
    double m_startMs = -1.0;
    double m_durationMs = 1.0;
    const CubicBezier* m_curve = nullptr;
};

namespace motion {

// Decelerazione: parte decisa e si posa dolcemente. È la curva che dà la
// sensazione "moderna"; conta più della durata.
// Attenzione alle curve più estreme come (0.1, 0.9, 0.2, 1): a 180 Hz fanno
// metà del movimento nei primi 3 frame e l'animazione non si vede più.
inline constexpr CubicBezier decelerate { 0.0, 0.0, 0.2, 1.0 };

// Apertura finestra: dissolvenza + risalita. Stessa durata di Theme.slow.
inline constexpr double windowOpenMs = 250.0;
inline constexpr int windowOpenRisePx = 36;

// Chiusura: la finestra si ritrae un poco e sparisce. Anche qui la curva di
// decelerazione: con una che parte piano la finestra sembrerebbe ignorare il
// clic per metà del tempo.
inline constexpr double windowCloseMs = 180.0;
inline constexpr double windowCloseScale = 0.94;

// Riduzione a icona e ripristino: la finestra vola verso il suo pulsante
// nella taskbar (e ritorno), rimpicciolendo e sfumando.
inline constexpr double windowMinimizeMs = 250.0;
inline constexpr double windowMinimizeScale = 0.3;

// Massimizzare e ripristinare: il contenuto di prima si deforma fino al
// riquadro nuovo e sfuma nella finestra vera.
inline constexpr double windowMaximizeMs = 250.0;

// Anteprima dello snap: cresce dal centro dell'area e si accende.
inline constexpr double snapPreviewMs = 150.0;

// Cambio di desktop: il desktop che si lascia scivola via sfumando, il nuovo
// arriva dall'altra parte. Lo scorrimento è una frazione dello schermo più
// stretto: con più schermi le finestre non invadono quello accanto.
inline constexpr double workspaceSwitchMs = 300.0;

// Lente di ingrandimento: da un ingrandimento all'altro.
inline constexpr double magnifierMs = 150.0;
inline constexpr double workspaceSlideFraction = 0.25;

} // namespace motion

} // namespace vela
