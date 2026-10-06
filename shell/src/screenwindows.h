// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QTimer>

#include <functional>

class QQmlEngine;
class QQuickWindow;
class QScreen;

// Una finestra della shell per ogni schermo, che nasce e sparisce con lo
// schermo: lo sfondo (Wallpaper.qml) e la taskbar (Taskbar.qml), come in
// Windows. Ogni finestra ha la proprietà `primary` (lo schermo principale:
// lì le icone del desktop, l'area di notifica). Se il compositor la chiude
// (schermo spento e riacceso, cambio di console) la si rimette.
class ScreenWindows : public QObject {
    Q_OBJECT

public:
    using Setup = std::function<void(QQuickWindow*, QScreen*)>;
    ScreenWindows(QQmlEngine* engine, const char* component, Setup setup, QObject* parent = nullptr);
    bool start(); // false se il QML non si carica

    // Solo sullo schermo principale (la taskbar, se l'utente la vuole lì sola).
    void setPrimaryOnly(bool on);

private:
    void sync();
    void scheduleSync();
    void remove(QScreen* screen);

    QQmlComponent m_component;
    QString m_name;
    Setup m_setup;
    bool m_primaryOnly = false;
    QHash<QScreen*, QPointer<QQuickWindow>> m_windows;
    QTimer m_syncTimer;
    int m_retries = 0; // riaperture senza uno schermo nuovo
};
