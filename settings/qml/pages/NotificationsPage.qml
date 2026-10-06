// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Sistema > Notifiche.
Page {
    CardGroup {
        Card {
            icon: "preferences-desktop-notification"
            title: qsTr("Notifications")
            description: qsTr("Alerts from apps and the system, in the bottom-right corner and in the notification center (Win+N)")
        }
        Card {
            icon: "notifications-disabled"
            title: qsTr("Do not disturb")
            description: qsTr("Notifications go straight to the notification center, without popping up")
            trailing: Toggle {
                checked: Prefs.doNotDisturb
                onToggled: on => Prefs.doNotDisturb = on
            }
        }
    }
}
