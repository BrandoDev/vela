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
        description: page.allDefault ? "È l'app predefinita per tutti i tipi che sa aprire"
            : "Rendila l'app predefinita per tutti i tipi di file e collegamenti che sa aprire"
        trailing: Button {
            text: "Imposta come predefinita"
            accent: true
            usable: !page.allDefault
            onClicked: DefaultApps.setDefaultForAll(DefaultApps.selectedApp)
        }
    }

    CardGroup {
        title: "Tipi di file e collegamenti"
        Repeater {
            model: page.types
            delegate: Card {
                required property var modelData
                icon: modelData.icon
                title: modelData.label
                description: (modelData.patterns ? modelData.patterns + " · " : "")
                    + (modelData.isDefault ? "Si apre con quest'app"
                        : modelData.current.name ? "Ora si apre con " + modelData.current.name
                        : "Nessuna app predefinita")
                trailing: Button {
                    visible: !modelData.isDefault
                    text: "Usa quest'app"
                    onClicked: DefaultApps.setDefaultForType(modelData.mime, DefaultApps.selectedApp)
                }
            }
        }
    }
}
