// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// The mouse's side buttons in every Vela app, like Windows: Back and Forward
// become the Back and Forward commands, the same the keyboard's Back and
// Forward keys send (Qt::Key_Back, Qt::Key_Forward). Qt counts them in
// QKeySequence::Back and Forward, with Alt+Left and Alt+Right, so an app
// supports the buttons, the keys and Alt+arrows at once with a Shortcut on
// `sequences: [StandardKey.Back]` (and Forward), whatever has the focus.
// `sequences`, not `sequence`: that one takes only the first binding
// (Alt+Left). Nothing to call: linking vela-app turns it on.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointer>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

namespace {

// The path a real key takes: shortcuts first, then the focused item. Queued:
// not inside the event being delivered now.
void sendCommand(QWindow* window, int key)
{
    QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyPress, key, Qt::NoModifier);
    QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyRelease, key, Qt::NoModifier);
}

class NavigationButtons : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        // Pressed on a window that wasn't active: the press activates it (any
        // button does, like on Windows), and the command waits for that,
        // since shortcuts only belong to the active window.
        if (type == QEvent::WindowActivate && watched == m_waiting) {
            if (m_since.elapsed() < 1000) {
                sendCommand(m_waiting, m_waitingKey);
            }
            m_waiting.clear();
            return false;
        }
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease
            && type != QEvent::MouseButtonDblClick) {
            return false;
        }
        // At the window, before its items: they never see the button.
        auto* window = qobject_cast<QWindow*>(watched);
        if (!window) {
            return false;
        }
        const Qt::MouseButton button = static_cast<QMouseEvent*>(event)->button();
        const int key = button == Qt::BackButton ? Qt::Key_Back : button == Qt::ForwardButton ? Qt::Key_Forward : 0;
        if (key == 0) {
            return false;
        }
        if (type == QEvent::MouseButtonPress) {
            if (window->isActive()) {
                sendCommand(window, key);
            } else {
                m_waiting = window;
                m_waitingKey = key;
                m_since.start();
            }
        }
        return true;
    }

private:
    QPointer<QWindow> m_waiting;
    int m_waitingKey = 0;
    QElapsedTimer m_since;
};

void install()
{
    QCoreApplication::instance()->installEventFilter(new NavigationButtons(QCoreApplication::instance()));
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(install)
