// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// L'aspetto delle app di Vela (Esplora), preso dalla shell: modalità chiara
// o scura delle app ("Scegli la modalità"), colore d'accento e Mica (vedi
// mica.h). Si aggiorna da solo quando le Impostazioni cambiano qualcosa o
// cambia lo sfondo. Le icone seguono la modalità (applyIconTheme).
class Appearance : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool light READ light NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor mica READ mica NOTIFY changed)
    Q_PROPERTY(QColor micaInactive READ micaInactive NOTIFY changed)
    // "l/" o "d/" per image://icon: cambia un attimo dopo la modalità, a
    // cache delle icone svuotate (applyIconTheme).
    Q_PROPERTY(QString iconMode READ iconMode NOTIFY iconModeChanged)

public:
    explicit Appearance(QObject* parent = nullptr);

    bool light() const { return m_light; }
    QColor accent() const { return m_accent; }
    QColor mica() const { return m_mica; }
    QColor micaInactive() const { return m_micaInactive; }
    QString iconMode() const { return m_iconMode; }

    // Comincia a osservare i file (dopo il primo fotogramma: l'avvio resta veloce).
    void watch();

signals:
    void changed();
    void iconModeChanged();

private:
    void reload();

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    bool m_watching = false;
    bool m_light = false;
    QColor m_accent;
    QColor m_mica;
    QColor m_micaInactive;
    QString m_iconMode = QStringLiteral("d/");
};
