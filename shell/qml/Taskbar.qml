import QtQuick

// La taskbar: pulsante Start e app fissate al centro, orologio a destra.
Window {
    id: root
    objectName: "taskbar"

    // Resta invisibile finché main.cpp non l'ha trasformata in un pannello
    // layer-shell. La larghezza la decide il compositor (ancorata ai lati).
    visible: false
    width: 1280
    height: Theme.taskbarHeight
    color: "transparent"

    // App fissate (id dei file .desktop). Quelle non installate si saltano.
    // TODO: renderle configurabili e trascinabili.
    readonly property var pinnedIds: [
        "org.kde.dolphin.desktop",
        "org.kde.konsole.desktop",
        "firefox.desktop",
        "org.mozilla.firefox.desktop",
        "chromium.desktop",
        "org.kde.kate.desktop",
        "systemsettings.desktop"
    ]
    readonly property var pinned: pinnedIds
        .map(id => ({ id: id, info: Apps.entry(id) }))
        .filter(item => item.info.name !== undefined)
        // la stessa app può esistere sia come pacchetto sia come Flatpak
        .filter((item, i, all) => all.findIndex(other => other.info.name === item.info.name) === i)

    Rectangle {
        anchors.fill: parent
        color: Theme.taskbar

        // Sottile riga di luce sul bordo superiore, come un vetro.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: Theme.stroke
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 4

        TaskbarButton {
            active: Shell.startMenuOpen
            onClicked: Shell.toggleStartMenu()
            StartGlyph { anchors.centerIn: parent }
        }

        Repeater {
            model: root.pinned

            TaskbarButton {
                required property var modelData
                onClicked: Apps.launchId(modelData.id)

                Image {
                    anchors.fill: parent
                    source: "image://icon/" + encodeURIComponent(modelData.info.iconName)
                    sourceSize: Qt.size(52, 52)
                    smooth: true
                    mipmap: true
                }
            }
        }
    }

    // Orologio: ora sopra, data sotto.
    Item {
        id: clock
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom; rightMargin: 12 }
        width: clockColumn.implicitWidth + 16

        property date now: new Date()

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: clock.now = new Date()
        }

        Rectangle {
            anchors { fill: parent; topMargin: 4; bottomMargin: 4 }
            radius: Theme.radiusSmall
            color: Theme.hover
            opacity: clockMouse.containsMouse ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }

        Column {
            id: clockColumn
            anchors.centerIn: parent
            spacing: 1

            Text {
                anchors.right: parent.right
                text: Qt.formatTime(clock.now, "HH:mm")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
            Text {
                anchors.right: parent.right
                text: Qt.formatDate(clock.now, "dd/MM/yyyy")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
        }

        MouseArea {
            id: clockMouse
            anchors.fill: parent
            hoverEnabled: true
        }
    }
}
