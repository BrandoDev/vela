// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Ora e lingua > Tastiera: i layout (Win+Spazio passa dall'uno
// all'altro), e la ripetizione dei tasti.
Page {
    id: page
    readonly property var layouts: Prefs.keyboardLayouts

    CardGroup {
        title: "Layout di tastiera"
        Card {
            icon: "input-keyboard"
            title: page.layouts.length === 0 ? "Quello del sistema" : "Layout installati"
            description: page.layouts.length > 1 ? "Win+Spazio per passare da un layout all'altro" : page.layouts.length === 0 ? "Scelto da localectl o da KDE" : ""
            trailing: Button {
                text: "Aggiungi una tastiera"
                onClicked: {
                    layoutSearch.text = ""
                    addDialog.open()
                    layoutSearch.forceActiveFocus()
                }
            }
        }
        Repeater {
            model: page.layouts
            delegate: Card {
                id: layoutCard
                required property var modelData
                required property int index
                icon: "input-keyboard"
                title: Layouts.name(modelData.layout, modelData.variant)
                description: index === 0 ? "Predefinito" : ""
                trailing: Row {
                    spacing: 4
                    Button {
                        visible: layoutCard.index > 0
                        subtle: true
                        text: "Sposta su"
                        onClicked: Prefs.moveKeyboardLayoutUp(layoutCard.index)
                    }
                    Button {
                        subtle: true
                        text: "Rimuovi"
                        onClicked: Prefs.removeKeyboardLayout(layoutCard.index)
                    }
                }
            }
        }
    }

    CardGroup {
        title: "Ripetizione dei tasti"
        Card {
            icon: "chronometer"
            title: "Ritardo di ripetizione"
            description: "Quanto tenere premuto un tasto prima che si ripeta: " + Prefs.repeatDelay + " ms"
            trailing: Slider {
                width: 220
                from: 150
                to: 1000
                stepSize: 50
                live: false
                value: Prefs.repeatDelay
                onReleased: value => Prefs.repeatDelay = Math.round(value)
            }
        }
        Card {
            icon: "chronometer"
            title: "Velocità di ripetizione"
            description: Prefs.repeatRate + " caratteri al secondo"
            trailing: Slider {
                width: 220
                from: 5
                to: 60
                stepSize: 1
                live: false
                value: Prefs.repeatRate
                onReleased: value => Prefs.repeatRate = Math.round(value)
            }
        }
        Card {
            icon: "edit-entry"
            title: "Prova"
            trailing: TextBox {
                width: 260
                placeholderText: "Tieni premuto un tasto qui"
            }
        }
    }

    Dialog {
        id: addDialog
        parent: root.contentItem
        title: "Aggiungi una tastiera"
        primaryText: ""
        secondaryText: "Annulla"
        dialogWidth: 520
        readonly property var all: visible ? Layouts.all() : []
        readonly property var matches: {
            const q = layoutSearch.text.trim().toLowerCase()
            const list = q === "" ? all : all.filter(l => l.name.toLowerCase().indexOf(q) >= 0 || l.layout === q)
            return list.slice(0, 200)
        }
        Column {
            width: parent.width
            spacing: 8
            TextBox {
                id: layoutSearch
                width: parent.width
                placeholderText: "Cerca (es. Italian, English)"
            }
            ListView {
                id: layoutList
                width: parent.width
                height: 320
                clip: true
                model: addDialog.matches
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: layoutEntry
                    required property var modelData
                    width: layoutList.width
                    height: 36
                    radius: Theme.radius
                    color: entryMouse.containsMouse ? Theme.subtleHover : "transparent"
                    Text {
                        x: 12
                        width: parent.width - 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: layoutEntry.modelData.name + "   " + layoutEntry.modelData.layout + (layoutEntry.modelData.variant !== "" ? "(" + layoutEntry.modelData.variant + ")" : "")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: entryMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            Prefs.addKeyboardLayout(layoutEntry.modelData.layout, layoutEntry.modelData.variant)
                            addDialog.close()
                        }
                    }
                }
            }
        }
    }
}
