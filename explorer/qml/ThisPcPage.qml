import QtQuick

// Questo PC, come Windows 11: le cartelle dell'utente e le unità con la
// barra dello spazio usato (rossa quando è quasi piena).
Flickable {
    id: page
    property var tab
    contentHeight: column.height + 32
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Component.onCompleted: Places.refreshDrives()
    Timer { interval: 5000; running: true; repeat: true; onTriggered: Places.refreshDrives() }

    Column {
        id: column
        x: 24
        y: 16
        width: page.width - 48
        spacing: 8

        Text {
            text: "Cartelle"
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
                    id: folder
                    required property var modelData
                    width: 220
                    height: 56
                    radius: Theme.radiusSmall
                    color: folderMouse.containsMouse ? Theme.hover : "transparent"
                    Image {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        height: 36
                        source: "image://fileicon/" + encodeURIComponent(folder.modelData.icon)
                        sourceSize: Qt.size(width, height)
                    }
                    Text {
                        x: 56
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 60
                        text: folder.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: folderMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onDoubleClicked: page.tab.navigate(folder.modelData.path)
                    }
                }
            }
        }

        Text {
            text: "Dispositivi e unità"
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
            topPadding: 20
            bottomPadding: 4
        }
        Flow {
            width: parent.width
            spacing: 4
            Repeater {
                model: Places.drives
                delegate: Rectangle {
                    id: drive
                    required property var modelData
                    readonly property real used: modelData.total > 0 ? 1 - modelData.free / modelData.total : 0
                    width: 300
                    height: 72
                    radius: Theme.radiusSmall
                    color: driveMouse.containsMouse ? Theme.hover : "transparent"
                    Image {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        source: "image://fileicon/" + encodeURIComponent(drive.modelData.icon)
                        sourceSize: Qt.size(width, height)
                    }
                    Column {
                        x: 60
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 72
                        spacing: 4
                        Text {
                            width: parent.width
                            text: drive.modelData.name + (drive.modelData.path !== "/" ? "  (" + drive.modelData.path + ")" : "")
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            width: parent.width
                            height: 12
                            color: Qt.rgba(1, 1, 1, 0.12)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                            Rectangle {
                                x: 1
                                y: 1
                                height: parent.height - 2
                                width: (parent.width - 2) * drive.used
                                color: drive.used > 0.9 ? "#d13438" : Theme.accentLight
                            }
                        }
                        Text {
                            text: Ops.formatSize(drive.modelData.free) + " disponibili di " + Ops.formatSize(drive.modelData.total)
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    MouseArea {
                        id: driveMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onDoubleClicked: page.tab.navigate(drive.modelData.path)
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                const p = mapToItem(null, mouse.x, mouse.y)
                                page.tab.menus.open([
                                    { text: "&Apri", icon: "document-open", action: () => page.tab.navigate(drive.modelData.path) },
                                    { text: "Apri in una nuova &scheda", icon: "tab-new", action: () => page.tab.openInNewTab(drive.modelData.path) },
                                    { separator: true },
                                    { text: "Apri in &Terminale", icon: "utilities-terminal", action: () => System.openTerminal(drive.modelData.path) },
                                    { text: "P&roprietà", icon: "document-properties", action: () => Ops.showProperties([drive.modelData.path]) }
                                ], p.x, p.y)
                            }
                        }
                    }
                }
            }
        }
    }
}
