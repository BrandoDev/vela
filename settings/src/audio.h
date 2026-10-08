// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

// Audio outputs and inputs for System > Sound, from PipeWire or PulseAudio
// through pactl (the same the taskbar uses). Updates itself when something
// changes (pactl subscribe).
class Audio : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    // [{name, description, volume (0-1.5), muted, isDefault}]
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)

public:
    explicit Audio(QObject* parent = nullptr);
    ~Audio() override;

    bool available() const { return m_available; }
    QVariantList outputs() const { return m_outputs; }
    QVariantList inputs() const { return m_inputs; }

    Q_INVOKABLE void refresh();
    // kind: "output" or "input"
    Q_INVOKABLE void setDefault(const QString& kind, const QString& name);
    Q_INVOKABLE void setVolume(const QString& kind, const QString& name, double volume);
    Q_INVOKABLE void setMuted(const QString& kind, const QString& name, bool muted);

signals:
    void changed();

private:
    QVariantList list(const QString& what, const QString& defaultName) const;

    bool m_available = false;
    QVariantList m_outputs;
    QVariantList m_inputs;
    QProcess m_subscribe;
    QTimer m_debounce;
};
