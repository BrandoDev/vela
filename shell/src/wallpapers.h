#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QTimer>

class QQmlEngine;
class QQuickWindow;
class QScreen;

// Uno sfondo per ogni schermo (Wallpaper.qml), che nasce e sparisce con lo
// schermo. Le icone del desktop stanno solo su quello principale, come in
// Windows. Se il compositor chiude uno sfondo (schermo spento e riacceso,
// cambio di console) lo si rimette.
class Wallpapers : public QObject {
    Q_OBJECT

public:
    explicit Wallpapers(QQmlEngine* engine, QObject* parent = nullptr);
    bool start(); // false se Wallpaper.qml non si carica

private:
    void sync();
    void scheduleSync();
    void remove(QScreen* screen);

    QQmlComponent m_component;
    QHash<QScreen*, QPointer<QQuickWindow>> m_windows;
    QTimer m_syncTimer;
    int m_retries = 0; // riaperture senza uno schermo nuovo
};
