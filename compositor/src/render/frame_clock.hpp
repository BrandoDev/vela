// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Il tempo di uno schermo (docs/renderer.md §4): quando verrà mostrato il
// frame che stiamo per disegnare, e quando cominciare a disegnarlo.
//
// - Le animazioni si calcolano per l'istante in cui il frame diventerà
//   luce, non per "adesso": il movimento è giusto al frame a qualunque
//   frequenza, anche se un frame arriva in ritardo (§4.2).
// - Si disegna il più tardi possibile (late latching, §4.3): si misura
//   quanto costa un frame (CPU e GPU, fino a quando è pronto) e si comincia
//   quel tanto, più un margine, prima del vblank. Input e animazioni
//   campionati più tardi = latenza minore. Se un frame arriva tardi il
//   margine cresce da solo, poi torna piano piano al minimo.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>

namespace vela::render {

inline int64_t toNs(const timespec& t)
{
    return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

inline int64_t nowNs()
{
    timespec t {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return toNs(t);
}

class FrameClock {
public:
    // Margine minimo tra "frame pronto" e vblank (VELA_LATCH_MARGIN, in ms).
    void setBaseMargin(int64_t ns) { m_baseMarginNs = std::max<int64_t>(0, ns); }

    // Periodo della modalità dello schermo (mHz), usato finché il backend
    // non ci dice il periodo vero con il feedback di presentazione.
    void setModeRefresh(int32_t refreshMhz)
    {
        const int64_t period = refreshMhz > 0 ? int64_t(1e12 / refreshMhz) : 0;
        if (period != m_modePeriodNs) {
            m_warmupPending = true;
        }
        m_modePeriodNs = period;
    }

    int64_t periodNs() const
    {
        if (m_presentedPeriodNs > 0) {
            return m_presentedPeriodNs;
        }
        return m_modePeriodNs > 0 ? m_modePeriodNs : 16'666'667; // sconosciuto: 60 Hz
    }

    // Il primo vblank della griglia (ultima presentazione vera + multipli
    // del periodo) dopo `t`. Senza presentazioni: `t` stesso.
    int64_t vblankAfter(int64_t t) const
    {
        const int64_t period = periodNs();
        if (m_lastPresentNs <= 0) {
            return t;
        }
        if (t < m_lastPresentNs) {
            return m_lastPresentNs - ((m_lastPresentNs - t) / period) * period;
        }
        return m_lastPresentNs + ((t - m_lastPresentNs) / period + 1) * period;
    }

    // Vblank di ritardo tra la consegna di un frame e la sua comparsa.
    // Sull'hardware di solito 0; in una finestra annidata il compositor
    // ospite ne aggiunge (KWin: 1 o 2).
    int latencyFrames() const { return m_latencyFrames; }

    // ------------------------------------------------ costo e margine --

    // Un frame misurato: dall'istante in cui doveva cominciare a quello in
    // cui era pronto (commit fatto e GPU finita).
    void addCost(int64_t now, int64_t costNs)
    {
        m_costs[m_nextCost] = { now, std::max<int64_t>(0, costNs) };
        m_nextCost = (m_nextCost + 1) % costSlots;
        m_costCount = std::min(m_costCount + 1, costSlots);
    }

    // Il costo di un frame: il massimo dell'ultimo secondo (almeno gli
    // ultimi 8 frame), perché un frame lento non deve far perdere il vblank.
    int64_t costNs(int64_t now) const
    {
        int64_t worst = 0;
        for (int i = 0; i < m_costCount; ++i) {
            const Cost& c = m_costs[(m_nextCost - 1 - i + costSlots) % costSlots];
            if (i >= 8 && now - c.at > 1'000'000'000) {
                break;
            }
            worst = std::max(worst, c.ns);
        }
        return worst;
    }

    int64_t marginNs() const { return m_baseMarginNs + m_extraMarginNs; }

    // Quanto prima del vblank si comincia: costo + margine, ma mai tanto da
    // saltare un vblank a ogni frame (subito dopo un vblank si fa sempre in
    // tempo per il successivo).
    int64_t budgetNs(int64_t now) const
    {
        return std::min(costNs(now) + marginNs(), maxBudgetNs());
    }

    // ----------------------------------------------------------- piano --

    struct Plan {
        int64_t start = 0; // quando cominciare a disegnare
        int64_t deadline = 0; // il vblank per cui si disegna
        int64_t present = 0; // quando il frame diventerà luce
    };

    // Il prossimo frame, se ne serve uno adesso. latch: si comincia il più
    // tardi possibile; altrimenti subito.
    Plan plan(int64_t now, bool latch) const
    {
        Plan p;
        if (latch) {
            const int64_t budget = budgetNs(now);
            p.deadline = vblankAfter(now + budget);
            p.start = std::max(now, p.deadline - budget);
        } else {
            p.deadline = vblankAfter(now + std::min(costNs(now), maxBudgetNs()));
            p.start = now;
        }
        if (m_lastPresentNs <= 0) {
            p.deadline = p.start + periodNs(); // nessuna griglia ancora: stima
        }
        p.present = p.deadline + int64_t(m_latencyFrames) * periodNs();
        return p;
    }

    // ------------------------------------------- consegna e comparsa --

    // Il frame per `commitSeq`, cominciato a `start`, doveva comparire a
    // `predicted`.
    void committed(uint32_t commitSeq, int64_t start, int64_t predicted)
    {
        m_pending[commitSeq % pendingSlots] = { commitSeq, start, predicted, true };
    }

    // Feedback del backend: il frame `commitSeq` è comparso a `when`.
    void presented(uint32_t commitSeq, int64_t when, int64_t refreshNs)
    {
        m_lastPresentNs = when;
        if (refreshNs > 0) {
            m_presentedPeriodNs = refreshNs;
        }
        ++m_frames;
        ++m_framesTotal;
        Pending& p = m_pending[commitSeq % pendingSlots];
        if (!p.valid || p.seq != commitSeq) {
            return;
        }
        p.valid = false;
        const int64_t period = periodNs();
        const int64_t error = when - p.predicted;
        // Il primo secondo dopo l'avvio o un cambio di modo non insegna nulla:
        // allocazioni, pipeline nuove e modeset fanno arrivare tardi qualche
        // frame una volta sola.
        if (m_warmupPending) {
            m_warmupUntilNs = when + 1'000'000'000;
            m_warmupPending = false;
        }
        const bool warmingUp = when < m_warmupUntilNs;
        if (error > period / 2) {
            // Arrivato tardi. Se il margine può ancora crescere è colpa
            // nostra (frame lento): margine più ampio. Se non può, il ritardo
            // è dello schermo (compositor ospite): lo impara la latenza.
            const int missed = int((error + period / 2) / period);
            m_missed += missed;
            m_missedTotal += missed;
            if (warmingUp) {
                // niente da imparare
            } else if (budgetNs(when) < maxBudgetNs()) {
                m_extraMarginNs = std::min(m_extraMarginNs + std::max<int64_t>(250'000, period / 10), period);
                m_shiftCandidate = 0;
                m_shiftCount = 0;
            } else {
                learnLatency(error);
            }
        } else {
            if (!warmingUp) {
                learnLatency(error);
            }
            // Puntuale: il margine in più torna giù di 0,25 ms al secondo.
            m_extraMarginNs = std::max<int64_t>(0, m_extraMarginNs - 250'000 * period / 1'000'000'000 - 1);
        }
        const double errorMs = double(error) / 1e6;
        m_errorSumMs += std::abs(errorMs);
        m_errorMaxMs = std::fmax(m_errorMaxMs, std::abs(errorMs));
        m_latencySumMs += double(when - p.start) / 1e6;
        ++m_measured;
    }

    // Statistiche dall'ultima chiamata (e azzera). false se è passato
    // troppo poco.
    struct Stats {
        double fps; // frame mostrati al secondo
        double errorMeanMs; // errore della previsione del momento di comparsa
        double errorMaxMs;
        double latencyMs; // dall'inizio del disegno alla luce, in media
        double costMs; // costo di un frame (il massimo recente)
        double marginMs;
        int missed; // vblank persi
    };
    // Dall'avvio, senza azzerare (per le prove: "state").
    uint64_t framesTotal() const { return m_framesTotal; }
    uint64_t missedTotal() const { return m_missedTotal; }

    bool takeStats(int64_t now, Stats& out)
    {
        if (m_statsStartNs == 0) {
            m_statsStartNs = now;
            return false;
        }
        const double seconds = double(now - m_statsStartNs) / 1e9;
        if (seconds < 2.0) {
            return false;
        }
        out.fps = m_frames / seconds;
        out.errorMeanMs = m_measured ? m_errorSumMs / m_measured : 0.0;
        out.errorMaxMs = m_errorMaxMs;
        out.latencyMs = m_measured ? m_latencySumMs / m_measured : 0.0;
        out.costMs = double(costNs(now)) / 1e6;
        out.marginMs = double(marginNs()) / 1e6;
        out.missed = m_missed;
        m_statsStartNs = now;
        m_frames = m_measured = m_missed = 0;
        m_errorSumMs = m_errorMaxMs = m_latencySumMs = 0.0;
        return true;
    }

private:
    int64_t maxBudgetNs() const
    {
        const int64_t period = periodNs();
        return period - std::min<int64_t>(500'000, period / 4);
    }

    // Se le ultime misure concordano su uno scarto di un numero intero di
    // vblank, quello scarto è la latenza dello schermo: si corregge la
    // previsione. Una misura isolata (un frame perso) non basta.
    void learnLatency(int64_t errorNs)
    {
        const int64_t period = periodNs();
        const int shift = int((errorNs + (errorNs >= 0 ? period / 2 : -period / 2)) / period);
        if (shift == 0 || shift != m_shiftCandidate) {
            m_shiftCandidate = shift;
            m_shiftCount = shift == 0 ? 0 : 1;
            return;
        }
        if (++m_shiftCount >= agreementNeeded) {
            m_latencyFrames = std::max(0, m_latencyFrames + shift);
            if (shift > 0) {
                // Il ritardo era dello schermo, non nostro: il margine
                // cresciuto per inseguirlo non serve più.
                m_extraMarginNs = 0;
            }
            m_shiftCandidate = 0;
            m_shiftCount = 0;
        }
    }

    static constexpr int agreementNeeded = 8;
    int m_latencyFrames = 0;
    int m_shiftCandidate = 0;
    int m_shiftCount = 0;

    bool m_warmupPending = true;
    int64_t m_warmupUntilNs = 0;
    int64_t m_baseMarginNs = 1'000'000;
    int64_t m_extraMarginNs = 0;
    static constexpr int costSlots = 64;
    struct Cost {
        int64_t at;
        int64_t ns;
    };
    Cost m_costs[costSlots] {};
    int m_nextCost = 0;
    int m_costCount = 0;

    static constexpr int pendingSlots = 8;
    struct Pending {
        uint32_t seq = 0;
        int64_t start = 0;
        int64_t predicted = 0;
        bool valid = false;
    };

    int64_t m_modePeriodNs = 0;
    int64_t m_presentedPeriodNs = 0;
    int64_t m_lastPresentNs = 0;
    Pending m_pending[pendingSlots] {};

    int64_t m_statsStartNs = 0;
    int m_frames = 0;
    int m_measured = 0;
    int m_missed = 0;
    uint64_t m_framesTotal = 0;
    uint64_t m_missedTotal = 0;
    double m_errorSumMs = 0.0;
    double m_errorMaxMs = 0.0;
    double m_latencySumMs = 0.0;
};

} // namespace vela::render
