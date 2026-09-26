#pragma once

// Il tempo di uno schermo (docs/renderer.md §4): quando verrà mostrato il
// frame che stiamo per disegnare. Le animazioni si calcolano per
// quell'istante, non per "adesso": così il movimento è giusto al frame a
// qualunque frequenza, anche se un frame arriva in ritardo.

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
    // Periodo della modalità dello schermo (mHz), usato finché il backend
    // non ci dice il periodo vero con il feedback di presentazione.
    void setModeRefresh(int32_t refreshMhz)
    {
        m_modePeriodNs = refreshMhz > 0 ? int64_t(1e12 / refreshMhz) : 0;
    }

    int64_t periodNs() const
    {
        if (m_presentedPeriodNs > 0) {
            return m_presentedPeriodNs;
        }
        return m_modePeriodNs > 0 ? m_modePeriodNs : 16'666'667; // sconosciuto: 60 Hz
    }

    // L'istante in cui il frame disegnato adesso diventerà luce: il primo
    // vblank dopo "adesso", contato dall'ultima presentazione vera, più i
    // vblank di ritardo che lo schermo ha dimostrato di avere.
    int64_t predict(int64_t now) const
    {
        const int64_t period = periodNs();
        const int64_t latency = int64_t(m_latencyFrames) * period;
        if (m_lastPresentNs <= 0 || m_lastPresentNs > now) {
            return now + period + latency;
        }
        const int64_t elapsed = now - m_lastPresentNs;
        const int64_t frames = elapsed / period + 1;
        return m_lastPresentNs + frames * period + latency;
    }

    // Vblank di ritardo tra la consegna di un frame e la sua comparsa.
    // Sull'hardware di solito 0; in una finestra annidata il compositor
    // ospite ne aggiunge (KWin: 2).
    int latencyFrames() const { return m_latencyFrames; }

    // Il frame per `commitSeq` doveva comparire a `predicted`.
    void committed(uint32_t commitSeq, int64_t predicted)
    {
        m_pending[commitSeq % pendingSlots] = { commitSeq, predicted, true };
    }

    // Feedback del backend: il frame `commitSeq` è comparso a `when`.
    void presented(uint32_t commitSeq, int64_t when, int64_t refreshNs)
    {
        m_lastPresentNs = when;
        if (refreshNs > 0) {
            m_presentedPeriodNs = refreshNs;
        }
        Pending& p = m_pending[commitSeq % pendingSlots];
        if (p.valid && p.seq == commitSeq) {
            learnLatency(when - p.predicted);
            const double errorMs = double(when - p.predicted) / 1e6;
            m_errorSumMs += std::abs(errorMs);
            m_errorMaxMs = std::fmax(m_errorMaxMs, std::abs(errorMs));
            ++m_measured;
            p.valid = false;
        }
        ++m_frames;
    }

    // Vblank passati senza un frame nuovo (siamo arrivati tardi).
    void missed(int count) { m_missed += count; }

    // Statistiche dall'ultima chiamata (e azzera): frame al secondo, errore
    // medio e massimo della previsione in ms, vblank persi. false se è
    // passato troppo poco.
    bool takeStats(int64_t now, double& fps, double& errorMeanMs, double& errorMaxMs, int& missedVblanks)
    {
        if (m_statsStartNs == 0) {
            m_statsStartNs = now;
            return false;
        }
        const double seconds = double(now - m_statsStartNs) / 1e9;
        if (seconds < 2.0) {
            return false;
        }
        fps = m_frames / seconds;
        errorMeanMs = m_measured ? m_errorSumMs / m_measured : 0.0;
        errorMaxMs = m_errorMaxMs;
        missedVblanks = m_missed;
        m_statsStartNs = now;
        m_frames = m_measured = m_missed = 0;
        m_errorSumMs = m_errorMaxMs = 0.0;
        return true;
    }

private:
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
            m_shiftCandidate = 0;
            m_shiftCount = 0;
        }
    }

    static constexpr int agreementNeeded = 8;
    int m_latencyFrames = 0;
    int m_shiftCandidate = 0;
    int m_shiftCount = 0;

    static constexpr int pendingSlots = 8;
    struct Pending {
        uint32_t seq = 0;
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
    double m_errorSumMs = 0.0;
    double m_errorMaxMs = 0.0;
};

} // namespace vela::render
