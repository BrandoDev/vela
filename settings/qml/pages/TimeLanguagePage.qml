// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Page {
    CardGroup {
        LinkCard {
            icon: "preferences-system-time"
            title: "Data e ora"
            description: "Fuso orario, impostazioni automatiche dell'orologio"
            onClicked: root.navigate("datetime")
        }
        LinkCard {
            icon: "input-keyboard"
            title: "Tastiera"
            description: "Layout di tastiera, ripetizione dei tasti"
            onClicked: root.navigate("keyboard")
        }
    }
}
