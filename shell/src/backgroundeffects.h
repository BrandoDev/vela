// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRegion>
#include <QVariantList>
#include <QWindow>

struct wl_registry;
struct wl_compositor;
struct ext_background_effect_manager_v1;
struct ext_background_effect_surface_v1;

// La sfocatura dietro i pannelli della shell (taskbar, menu Start, menu,
// notifiche, Alt+Tab), chiesta al compositor con il protocollo standard
// ext-background-effect-v1 (docs/renderer.md §8.3). Il compositor sfoca ciò
// che sta dietro la regione e la ritaglia con l'alfa del pannello: gli
// angoli arrotondati restano tali.
//
// Dal QML: Effects.setBlur(finestra, [Qt.rect(...), ...]) ogni volta che la
// zona cambia; Effects.blurAvailable dice se il compositor lo sa fare (se
// no, i pannelli tornano opachi).
class BackgroundEffects : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool blurAvailable READ blurAvailable NOTIFY blurAvailableChanged)

public:
    explicit BackgroundEffects(QObject* parent = nullptr);
    ~BackgroundEffects() override;

    bool blurAvailable() const { return m_blurAvailable; }
    // Rettangoli in coordinate della finestra; lista vuota: niente sfocatura.
    Q_INVOKABLE void setBlur(QWindow* window, const QVariantList& rects);

signals:
    void blurAvailableChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend struct EffectCallbacks;
    struct Entry {
        QPointer<QWindow> window;
        QRegion region;
        ext_background_effect_surface_v1* effect = nullptr;
        bool connected = false;
        bool following = false; // collegati ai segnali della wl_surface
    };
    void follow(Entry& entry);
    void apply(Entry& entry);
    void drop(Entry& entry);

    wl_registry* m_registry = nullptr;
    wl_compositor* m_compositor = nullptr;
    ext_background_effect_manager_v1* m_manager = nullptr;
    bool m_blurAvailable = false;
    QHash<QWindow*, Entry> m_entries;
};
