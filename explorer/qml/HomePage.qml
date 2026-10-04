import QtQuick

// La Home di Esplora, come Windows 11: le cartelle di Accesso rapido in
// riquadri, poi i file usati di recente.
Flickable {
    id: page
    property var tab
    contentHeight: column.height + 32
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    readonly property var recent: Places.recentFiles(40)

    Column {
        id: column
        x: 24
        y: 16
        width: page.width - 48
        spacing: 8

        Text {
            text: "Accesso rapido"
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
            bottomPadding: 4
        }
        Flow {
            width: parent.width
            spacing: 4
            Repeater {
                model: Places.quickAccess
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    width: 220
                    height: 64
                    radius: Theme.radiusSmall
                    color: tileMouse.containsMouse ? Theme.hover : "transparent"
                    Image {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        source: "image://fileicon/" + encodeURIComponent(tile.modelData.icon)
                        sourceSize: Qt.size(width, height)
                    }
                    Column {
                        x: 60
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 66
                        Text {
                            width: parent.width
                            text: tile.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: "Aggiunta"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    MouseArea {
                        id: tileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                        onDoubleClicked: page.tab.navigate(tile.modelData.path)
                        onClicked: mouse => {
                            if (mouse.button === Qt.MiddleButton) {
                                page.tab.openInNewTab(tile.modelData.path)
                            } else if (mouse.button === Qt.RightButton) {
                                const p = mapToItem(null, mouse.x, mouse.y)
                                page.tab.menus.open([
                                    { text: "&Apri", icon: "document-open", action: () => page.tab.navigate(tile.modelData.path) },
                                    { text: "Apri in una nuova &scheda", icon: "tab-new", action: () => page.tab.openInNewTab(tile.modelData.path) },
                                    { separator: true },
                                    { text: "&Rimuovi da Accesso rapido", icon: "window-unpin", action: () => Places.unpin(tile.modelData.path) },
                                    { text: "P&roprietà", icon: "document-properties", action: () => Ops.showProperties([tile.modelData.path]) }
                                ], p.x, p.y)
                            }
                        }
                    }
                    DropArea {
                        anchors.fill: parent
                        onEntered: drag => { if (!drag.hasUrls) drag.accepted = false }
                        onDropped: drop => {
                            Ops.drop(drop.urls.map(u => u.toString()), tile.modelData.path, 0)
                            drop.accept(Qt.MoveAction)
                        }
                    }
                }
            }
        }

        Text {
            text: "Recenti"
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
            topPadding: 20
            bottomPadding: 4
        }
        Text {
            visible: page.recent.length === 0
            text: "I file aperti di recente compariranno qui."
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
        }
        Repeater {
            model: page.recent
            delegate: Rectangle {
                id: recentRow
                required property var modelData
                width: column.width
                height: 32
                radius: Theme.radiusSmall
                color: recentMouse.containsMouse ? Theme.hover : "transparent"
                Image {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    source: "image://fileicon/" + encodeURIComponent(recentRow.modelData.icon)
                    sourceSize: Qt.size(width, height)
                }
                Text {
                    x: 34
                    width: parent.width * 0.38
                    anchors.verticalCenter: parent.verticalCenter
                    text: recentRow.modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    elide: Text.ElideRight
                }
                Text {
                    x: parent.width * 0.42
                    width: 150
                    anchors.verticalCenter: parent.verticalCenter
                    text: Qt.formatDateTime(recentRow.modelData.modified, "dd/MM/yyyy HH:mm")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall + 1
                }
                Text {
                    x: parent.width * 0.42 + 160
                    width: parent.width - x - 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: recentRow.modelData.location
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall + 1
                    elide: Text.ElideMiddle
                }
                MouseArea {
                    id: recentMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onDoubleClicked: Ops.open([recentRow.modelData.path])
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            const p = mapToItem(null, mouse.x, mouse.y)
                            page.tab.menus.open([
                                { text: "&Apri", icon: "document-open", action: () => Ops.open([recentRow.modelData.path]) },
                                { text: "Apri &percorso file", icon: "folder-open", action: () => page.tab.navigate(recentRow.modelData.location, recentRow.modelData.path) },
                                { separator: true },
                                { text: "Copia come &percorso", icon: "edit-copy-path", action: () => Ops.copyAsPath([recentRow.modelData.path]) },
                                { text: "P&roprietà", icon: "document-properties", action: () => Ops.showProperties([recentRow.modelData.path]) }
                            ], p.x, p.y)
                        }
                    }
                }
            }
        }
    }
}
