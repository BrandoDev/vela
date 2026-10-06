// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// App > App predefinite, come Windows 11: un tipo di file o di collegamento
// preciso, gli usi comuni, e le app una per una (DefaultAppPage.qml).
Page {
    id: page

    // Si ricalcolano quando una scelta cambia (DefaultApps.changed).
    readonly property var result: {
        DefaultApps.categories
        return DefaultApps.lookup(typeField.text)
    }
    readonly property var appList: {
        DefaultApps.categories
        return DefaultApps.apps(appFilter.text)
    }

    Text {
        width: parent.width
        text: "Scegli le app che aprono le pagine web, la posta, la musica, i video, le foto, i PDF e gli altri file. Le scelte valgono per tutte le app."
        color: Theme.textSecondary
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
        bottomPadding: 8
    }

    // --- un tipo preciso: ".mkv", "mp3", "mailto" ---
    CardGroup {
        title: "Imposta un valore predefinito per un tipo di file o di collegamento"
        Card {
            icon: "search"
            title: "Tipo di file o di collegamento"
            description: "Per esempio .pdf, mp3, .mkv o mailto"
            trailing: TextBox {
                id: typeField
                width: 220
                placeholderText: "Scrivi un tipo"
            }
        }
        Card {
            visible: typeField.text.trim() !== "" && page.result.found
            icon: page.result.icon || "unknown"
            title: page.result.label || ""
            description: (page.result.patterns ? page.result.patterns + " · " : "") + (page.result.mime || "")
            trailing: Choice {
                usable: (page.result.candidates || []).length > 0
                model: (page.result.candidates || []).map(a => ({ text: a.name }))
                currentIndex: (page.result.candidates || []).findIndex(a => a.id === (page.result.current || {}).id)
                displayText: currentIndex >= 0 ? page.result.candidates[currentIndex].name
                    : usable ? "Scegli un'app" : "Nessuna app lo apre"
                onChosen: index => DefaultApps.setDefaultForType(page.result.mime, page.result.candidates[index].id)
            }
        }
        Card {
            visible: typeField.text.trim() !== "" && !page.result.found
            icon: "dialog-information"
            title: "Nessun tipo trovato per «" + typeField.text.trim() + "»"
            description: "Prova con l'estensione del file (.pdf) o con il nome di un collegamento (mailto, https)."
        }
    }

    // --- gli usi comuni ---
    CardGroup {
        title: "Usi comuni"
        Repeater {
            model: DefaultApps.categories
            delegate: Card {
                id: category
                required property var modelData
                icon: modelData.current.icon || modelData.icon
                title: modelData.label
                description: modelData.current.name || (modelData.candidates.length > 0 ? "Nessuna app scelta" : "Nessuna app installata lo apre")
                trailing: Choice {
                    usable: category.modelData.candidates.length > 0
                    model: category.modelData.candidates.map(a => ({ text: a.name }))
                    currentIndex: category.modelData.candidates.findIndex(a => a.id === category.modelData.current.id)
                    displayText: currentIndex >= 0 ? category.modelData.candidates[currentIndex].name : "Scegli un'app"
                    onChosen: index => DefaultApps.setDefault(category.modelData.key, category.modelData.candidates[index].id)
                }
            }
        }
    }

    // --- per app ---
    CardGroup {
        title: "App"
        Item {
            width: parent.width
            height: 44
            TextBox {
                id: appFilter
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(360, parent.width)
                placeholderText: "Cerca app"
            }
        }
        Repeater {
            model: page.appList
            delegate: Card {
                required property var modelData
                icon: modelData.icon
                title: modelData.name
                description: modelData.count === 1 ? "Apre 1 tipo di file o di collegamento"
                    : "Apre " + modelData.count + " tipi di file e collegamenti"
                clickable: true
                chevron: true
                onClicked: {
                    DefaultApps.selectedApp = modelData.id
                    root.navigate("default-app")
                }
            }
        }
    }
}
