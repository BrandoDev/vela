// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Snap Assist di Windows 11: appena una finestra si aggancia, negli spazi
// rimasti liberi compaiono le altre finestre, in anteprima; un clic aggancia
// quella scelta lì, e si passa allo spazio successivo. Esc o un clic altrove
// chiude. Gli spazi sono in dodicesimi dell'area utile dello schermo (la
// finestra copre proprio quella: la taskbar resta fuori).
Window {
    id: root
    objectName: "snapAssist"
    visible: false
    color: "transparent"

    property var zones: [] // gli spazi da riempire, nell'ordine
    property int current: 0 // quello di cui si scelgono le finestre
    property var candidates: [] // [{id, title, icon}]
    property int revision: 0
    property string origin: "" // la finestra agganciata da cui è partito

    function overlaps(a, b) {
        return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3]
    }

    // Gli spazi liberi: quelli del layout scelto (se lo snap viene dai
    // layout), altrimenti il complemento di una metà o di un quarto.
    function freeZones(state) {
        const tile = state.tile
        let zones
        if (Menus.snapLayout && Menus.snapLayout.window === state.window) {
            zones = Menus.snapLayout.zones
        } else if (tile[1] === 0 && tile[3] === 12) {
            zones = [[0, 0, 6, 12], [6, 0, 12, 12]]
        } else if (tile[2] - tile[0] === 6 && tile[3] - tile[1] === 6) {
            zones = [[0, 0, 6, 6], [6, 0, 12, 6], [0, 6, 6, 12], [6, 6, 12, 12]]
        } else {
            zones = []
        }
        Menus.snapLayout = null
        return zones.filter(z => !overlaps(z, tile) && !state.occupied.some(o => overlaps(z, o)))
    }

    Connections {
        target: Shell
        function onSnapAssistRequested(json) {
            const state = JSON.parse(json)
            const zones = root.freeZones(state)
            if (zones.length === 0 || state.candidates.length === 0) {
                return
            }
            const screen = Qt.application.screens.find(s => s.name === state.output)
            if (screen && screen !== root.screen) {
                root.visible = false
                Shell.placeOnScreen(root, screen.name)
            }
            root.zones = zones
            root.origin = state.window
            root.current = 0
            root.candidates = state.candidates.map(id => ({
                id: id, title: Capture.title(id), icon: Apps.iconForAppId(Capture.appId(id))
            }))
            Capture.capture(state.candidates)
            root.visible = true
            content.opacity = 0
            appear.restart()
            content.forceActiveFocus()
            root.updateBlur()
        }
    }
    Connections {
        target: Capture
        function onThumbnailReady(identifier) { root.revision++ }
    }

    function close() {
        visible = false
        zones = []
    }
    function pick(id) {
        const z = zones[current]
        // Accanto alla finestra appena agganciata: insieme fanno un gruppo di snap.
        Shell.windowAction(id, "snap " + z.join(" ") + " quiet " + origin)
        candidates = candidates.filter(c => c.id !== id)
        if (current + 1 < zones.length && candidates.length > 0) {
            current++
            updateBlur()
        } else {
            close()
        }
    }
    function zoneRect(z) {
        return Qt.rect(Math.round(width * z[0] / 12), Math.round(height * z[1] / 12),
            Math.round(width * z[2] / 12) - Math.round(width * z[0] / 12),
            Math.round(height * z[3] / 12) - Math.round(height * z[1] / 12))
    }
    function updateBlur() {
        if (visible) {
            Effects.setBlur(root, zones.slice(current).map(z => {
                const r = zoneRect(z)
                return Qt.rect(r.x + 4, r.y + 4, r.width - 8, r.height - 8)
            }))
        }
    }
    onWidthChanged: updateBlur()
    onHeightChanged: updateBlur()
    onActiveChanged: if (!active && visible && !Menus.isOpen) close()

    NumberAnimation { id: appear; target: content; property: "opacity"; to: 1; duration: Theme.normal }

    Item {
        id: content
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: root.close()

        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }

        Repeater {
            model: root.zones
            delegate: Rectangle {
                id: zone
                required property var modelData
                required property int index
                readonly property rect area: root.zoneRect(modelData)
                readonly property bool active: index === root.current
                visible: index >= root.current
                x: area.x + 4
                y: area.y + 4
                width: area.width - 8
                height: area.height - 8
                radius: Theme.radiusLarge
                color: Theme.light ? (active ? Qt.rgba(0.94, 0.94, 0.96, 0.78) : Qt.rgba(0.94, 0.94, 0.96, 0.5))
                    : (active ? Qt.rgba(0.12, 0.12, 0.14, 0.72) : Qt.rgba(0.12, 0.12, 0.14, 0.45))
                border.width: 1
                border.color: Theme.stroke

                MouseArea { anchors.fill: parent } // dentro uno spazio non si chiude

                // Le finestre da proporre, in anteprima, nello spazio attivo.
                Item {
                    id: space
                    visible: zone.active
                    anchors { fill: parent; margins: 24 }
                    // Colonne quante ne servono per riempire lo spazio con schede 16:10.
                    readonly property int columns: Math.max(1, Math.min(root.candidates.length,
                        Math.ceil(Math.sqrt(root.candidates.length * width / Math.max(1, height) / 1.4))))
                    readonly property int rows: Math.ceil(root.candidates.length / columns)
                    readonly property real cardWidth: Math.min(320, (width - 16 * (columns - 1)) / columns,
                        ((height - 16 * (rows - 1)) / rows - 34) / 0.62)
                Grid {
                    anchors.centerIn: parent
                    columns: space.columns
                    spacing: 16

                    Repeater {
                        model: root.candidates
                        delegate: Rectangle {
                            id: card
                            required property var modelData
                            width: space.cardWidth
                            height: width * 0.62 + 34
                            radius: Theme.radiusLarge
                            color: cardMouse.containsMouse ? Theme.cardHover : Theme.card
                            border.width: cardMouse.containsMouse ? 2 : 1
                            border.color: cardMouse.containsMouse ? Theme.accent : Theme.stroke
                            Row {
                                x: 10
                                y: 8
                                width: parent.width - 20
                                spacing: 8
                                Image {
                                    width: 16
                                    height: 16
                                    source: Theme.icons + encodeURIComponent(card.modelData.icon)
                                    sourceSize: Qt.size(width, height)
                                }
                                Text {
                                    width: parent.width - 24
                                    text: card.modelData.title
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                            }
                            Image {
                                x: 8
                                y: 32
                                width: parent.width - 16
                                height: parent.height - 40
                                source: root.revision > 0 ? "image://thumbnail/" + card.modelData.id + "/" + root.revision : ""
                                fillMode: Image.PreserveAspectFit
                                cache: false
                            }
                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.pick(card.modelData.id)
                            }
                        }
                    }
                }
                }
            }
        }
    }
}
