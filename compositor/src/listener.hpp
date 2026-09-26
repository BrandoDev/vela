#pragma once

#include "wlr.hpp"

#include <functional>
#include <utility>

namespace vela {

// Wrapper RAII attorno a wl_listener.
//
// - Si collega a un segnale con una lambda: niente wl_container_of.
// - Si scollega da solo quando viene distrutto. wlroots >= 0.19 verifica con
//   assert che nessun listener resti attaccato a un oggetto che muore, quindi
//   questo evita un'intera categoria di crash.
//
// Regola d'uso: se una callback distrugge l'oggetto che possiede il
// Listener (tipico `delete this` nell'handler di destroy), deve farlo come
// ultima istruzione e non toccare più nulla dopo.
class Listener {
public:
    using Callback = std::function<void(void*)>;

    Listener()
    {
        m_slot.owner = this;
        m_slot.listener.notify = &Listener::dispatch;
        wl_list_init(&m_slot.listener.link);
    }

    ~Listener() { disconnect(); }

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;

    void connect(wl_signal* signal, Callback callback)
    {
        disconnect();
        m_callback = std::move(callback);
        wl_signal_add(signal, &m_slot.listener);
    }

    void disconnect()
    {
        if (!wl_list_empty(&m_slot.listener.link)) {
            wl_list_remove(&m_slot.listener.link);
            wl_list_init(&m_slot.listener.link);
        }
    }

private:
    // Struttura standard-layout con wl_listener come primo membro: da un
    // wl_listener* possiamo risalire allo Slot con un semplice cast.
    struct Slot {
        wl_listener listener;
        Listener* owner;
    };

    static void dispatch(wl_listener* listener, void* data)
    {
        auto* slot = reinterpret_cast<Slot*>(listener);
        slot->owner->m_callback(data);
    }

    Slot m_slot {};
    Callback m_callback;
};

} // namespace vela
