// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenwindows.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QtDebug>

namespace {

bool realScreen(const QScreen* screen)
{
    return screen && !screen->name().isEmpty(); // senza nome: il segnaposto di Qt
}

} // namespace

ScreenWindows::ScreenWindows(QQmlEngine* engine, const char* component, Setup setup, QObject* parent)
    : QObject(parent)
    , m_component(engine, "Vela.Shell", component)
    , m_name(QString::fromLatin1(component))
    , m_setup(std::move(setup))
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(100); // gli schermi arrivano uno alla volta
    connect(&m_syncTimer, &QTimer::timeout, this, &ScreenWindows::sync);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
        m_retries = 0;
        scheduleSync();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ScreenWindows::remove);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &ScreenWindows::scheduleSync);
}

bool ScreenWindows::start()
{
    if (m_component.isError()) {
        qCritical() << "vela-shell:" << m_name << m_component.errors();
        return false;
    }
    sync();
    return true;
}

void ScreenWindows::setPrimaryOnly(bool on)
{
    if (on != m_primaryOnly) {
        m_primaryOnly = on;
        scheduleSync();
    }
}

void ScreenWindows::scheduleSync()
{
    m_syncTimer.start();
}

void ScreenWindows::remove(QScreen* screen)
{
    if (QPointer<QQuickWindow> window = m_windows.take(screen)) {
        window->deleteLater();
    }
}

void ScreenWindows::sync()
{
    QScreen* primary = QGuiApplication::primaryScreen();
    for (QScreen* screen : QGuiApplication::screens()) {
        if (!realScreen(screen)) {
            continue;
        }
        if (m_primaryOnly && screen != primary) {
            remove(screen);
            continue;
        }
        QPointer<QQuickWindow>& window = m_windows[screen];
        if (!window) {
            QObject* object = m_component.create();
            window = qobject_cast<QQuickWindow*>(object);
            if (!window) {
                delete object;
                m_windows.remove(screen);
                continue;
            }
            static_cast<QObject*>(window)->setParent(this); // proprietà, non finestra madre
            qInfo("vela-shell: %s su %s%s", qPrintable(m_name), qPrintable(screen->name()),
                screen == primary ? " (principale)" : "");
            window->setScreen(screen);
            m_setup(window, screen);
            // Chiuso dal compositor: lo si rimette, ma senza insistere se
            // continua a chiuderlo.
            connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
                if (!visible && m_retries < 5) {
                    ++m_retries;
                    scheduleSync();
                }
            });
        }
        window->setProperty("primary", screen == primary);
        if (!window->isVisible()) {
            window->setScreen(screen);
            window->show();
        }
    }
}
