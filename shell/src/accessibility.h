// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

// Luce notturna, filtri colore, lente di ingrandimento e tasti permanenti,
// come li vede la shell: li applica il compositor
// (compositor/src/accessibility.cpp), che a ogni cambiamento manda lo stato
// in JSON ("accessibility <json>"); da qui le impostazioni rapide li
// accendono e spengono.
class Accessibility : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool nightLight READ nightLight WRITE setNightLight NOTIFY changed)
    Q_PROPERTY(bool colorFilter READ colorFilter WRITE setColorFilter NOTIFY changed)
    Q_PROPERTY(bool magnifier READ magnifier WRITE setMagnifier NOTIFY changed)
    Q_PROPERTY(bool stickyKeys READ stickyKeys WRITE setStickyKeys NOTIFY changed)

public:
    explicit Accessibility(QObject* parent = nullptr);

    bool nightLight() const { return m_nightLight; }
    void setNightLight(bool on);
    bool colorFilter() const { return m_colorFilter; }
    void setColorFilter(bool on);
    bool magnifier() const { return m_magnifier; }
    void setMagnifier(bool on);
    bool stickyKeys() const { return m_stickyKeys; }
    void setStickyKeys(bool on);

    // Lo stato mandato dal compositor.
    void update(const QByteArray& json);
    // Lo chiede al compositor (all'avvio della shell).
    void query();

signals:
    void changed();

private:
    bool m_nightLight = false;
    bool m_colorFilter = false;
    bool m_magnifier = false;
    bool m_stickyKeys = false;
};
