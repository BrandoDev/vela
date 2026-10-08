// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

// Night light, color filters, magnifier and sticky keys as the shell sees
// them: the compositor applies them (compositor/src/a11y.c) and sends the
// state as JSON on every change ("accessibility <json>"); quick settings turn
// them on and off from here.
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

    // The state sent by the compositor.
    void update(const QByteArray& json);
    // Asks the compositor for it (when the shell starts).
    void query();

signals:
    void changed();

private:
    bool m_nightLight = false;
    bool m_colorFilter = false;
    bool m_magnifier = false;
    bool m_stickyKeys = false;
};
