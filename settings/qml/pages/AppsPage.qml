// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

Page {
    CardGroup {
        LinkCard {
            icon: "view-list-details"
            title: qsTr("Installed apps")
            description: qsTr("Uninstall and search apps")
            onClicked: root.navigate("installed-apps")
        }
        LinkCard {
            icon: "preferences-desktop-default-applications"
            title: qsTr("Default apps")
            description: qsTr("The apps that open web pages, mail, music, photos and other files")
            onClicked: root.navigate("default-apps")
        }
    }
}
