// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// App > App predefinite > un'app, come Windows 11: i tipi di file e di
// collegamento che sa aprire, con chi li apre oggi, e "Imposta come
// predefinita" per tutti in una volta.
Page {
    id: page

    readonly property var types: {
        DefaultApps.categories // si ricalcola quando una scelta cambia
        return DefaultApps.typesOf(DefaultApps.selectedApp)
    }
    readonly property bool allDefault: types.length > 0 && types.every(t => t.isDefault)

    Card {
        icon: DefaultApps.selectedIcon
        title: DefaultApps.selectedName
        description: page.allDefault ? qsTr("It's the default app for every type it can open")
            : qsTr("Make it the default app for every file and link type it can open")
        trailing: Button {
            text: qsTr("Set as default")
            accent: true
            usable: !page.allDefault
            onClicked: DefaultApps.setDefaultForAll(DefaultApps.selectedApp)
        }
    }

    CardGroup {
        title: qsTr("File and link types")
        Repeater {
            model: page.types
            delegate: Card {
                required property var modelData
                icon: modelData.icon
                title: modelData.label
                description: (modelData.patterns ? modelData.patterns + " · " : "")
                    + (modelData.isDefault ? qsTr("Opens with this app")
                        : modelData.current.name ? qsTr("Currently opens with ") + modelData.current.name
                        : qsTr("No default app"))
                trailing: Button {
                    visible: !modelData.isDefault
                    text: qsTr("Use this app")
                    onClicked: DefaultApps.setDefaultForType(modelData.mime, DefaultApps.selectedApp)
                }
            }
        }
    }
}
