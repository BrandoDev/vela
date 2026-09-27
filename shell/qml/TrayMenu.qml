import QtQuick
import QtQuick.Shapes

// Il menu di un'icona dell'area di notifica. La finestra copre lo schermo
// ed è trasparente: il menu sta sopra l'icona, e un clic fuori lo chiude.
Window {
    id: root
    objectName: "trayMenu"
    visible: false
    color: "transparent"

    property int row: -1
    property int anchorX: 0
    // I sottomenu si aprono al posto del menu, con una voce per tornare.
    property var levels: []
    readonly property var entries: levels.length > 0 ? levels[levels.length - 1] : []

    function close() {
        visible = false
        levels = []
    }

    Connections {
        target: Tray
        function onMenuReady(row, anchorX, entries) {
            root.row = row
            root.anchorX = anchorX
            root.levels = [entries]
            root.visible = true
            panel.forceActiveFocus()
        }
    }

    onActiveChanged: {
        if (!active && visible) {
            close()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.close()
    }

    Rectangle {
        id: panel
        width: 260
        height: list.implicitHeight + 8
        x: Math.max(8, Math.min(root.width - width - 8, root.anchorX - width / 2))
        y: root.height - Theme.taskbarHeight - height - 8
        radius: Theme.radiusLarge
        color: Theme.popup
        border.width: 1
        border.color: Theme.stroke
        focus: true
        Keys.onEscapePressed: root.close()

        // I clic dentro il pannello non chiudono il menu.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            id: list
            x: 4
            y: 4
            width: parent.width - 8

            // Dentro un sottomenu: si torna al livello sopra.
            Item {
                visible: root.levels.length > 1
                width: parent.width
                height: visible ? 34 : 0
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: Theme.hover
                    opacity: backMouse.containsMouse ? 1 : 0
                }
                Text {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: "‹  Indietro"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontNormal
                }
                MouseArea {
                    id: backMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.levels = root.levels.slice(0, root.levels.length - 1)
                }
            }

            Repeater {
                model: root.entries

                delegate: Item {
                    id: entry
                    required property var modelData
                    readonly property bool hasChildren: modelData.children.length > 0
                    width: list.width
                    height: modelData.separator ? 9 : 34

                    Rectangle {
                        visible: entry.modelData.separator
                        anchors.verticalCenter: parent.verticalCenter
                        x: 8
                        width: parent.width - 16
                        height: 1
                        color: Theme.stroke
                    }

                    Rectangle {
                        visible: !entry.modelData.separator
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: entryMouse.pressed ? Theme.pressed : Theme.hover
                        opacity: entryMouse.containsMouse && entry.modelData.enabled ? 1 : 0
                    }

                    // Spunta o pallino delle voci attivabili, altrimenti l'icona.
                    Item {
                        visible: !entry.modelData.separator
                        x: 10
                        width: 16
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            anchors.fill: parent
                            visible: !entry.modelData.checkable && entry.modelData.icon !== ""
                            source: entry.modelData.icon
                            sourceSize: Qt.size(32, 32)
                        }
                        Shape {
                            anchors.fill: parent
                            visible: entry.modelData.checkable && entry.modelData.checked && !entry.modelData.radio
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: Theme.text
                                strokeWidth: 1.5
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                startX: 3; startY: 8.5
                                PathLine { x: 6.5; y: 12 }
                                PathLine { x: 13; y: 4.5 }
                            }
                        }
                        Rectangle {
                            visible: entry.modelData.checkable && entry.modelData.checked && entry.modelData.radio
                            anchors.centerIn: parent
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.text
                        }
                    }

                    Text {
                        visible: !entry.modelData.separator
                        x: 36
                        width: parent.width - 36 - 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.label
                        color: entry.modelData.enabled ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: entry.hasChildren
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: "›"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                    }

                    MouseArea {
                        id: entryMouse
                        anchors.fill: parent
                        enabled: !entry.modelData.separator && entry.modelData.enabled
                        hoverEnabled: true
                        onClicked: {
                            if (entry.hasChildren) {
                                root.levels = root.levels.concat([entry.modelData.children])
                                return
                            }
                            Tray.activateMenuEntry(root.row, entry.modelData.id)
                            root.close()
                        }
                    }
                }
            }
        }
    }
}
