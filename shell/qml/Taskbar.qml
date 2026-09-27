import QtQuick

// La taskbar: pulsante Start, app fissate e app aperte al centro, orologio
// a destra.
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
    readonly property list<string> pinnedIds: [
        "org.kde.dolphin.desktop",
        "org.kde.konsole.desktop",
        "firefox.desktop",
        "org.mozilla.firefox.desktop",
        "chromium.desktop",
        "org.kde.kate.desktop",
        "systemsettings.desktop"
    ]
    Component.onCompleted: Tasks.pinnedIds = pinnedIds

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
        id: buttons
        // Centrata a mano invece che con anchors: così, quando un'app si
        // apre o si chiude, la fila scivola al nuovo centro invece di saltare.
        x: Math.round((parent.width - width) / 2)
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Behavior on x {
            NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        }
        // Il pulsante di un'app appena aperta entra crescendo.
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal }
            NumberAnimation {
                property: "scale"; from: 0.5; to: 1; duration: Theme.slow
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
            }
        }
        move: Transition {
            NumberAnimation {
                property: "x"; duration: Theme.normal
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
            }
        }

        TaskbarButton {
            active: Shell.startMenuOpen
            onClicked: Shell.toggleStartMenu()
            StartGlyph { anchors.centerIn: parent }
        }

        Repeater {
            model: Tasks

            TaskbarButton {
                id: task
                required property int index
                required property string name
                required property string iconName
                required property int windowCount
                required property bool windowActive

                tooltip: name
                running: windowCount > 0
                active: windowActive
                onClicked: Tasks.activate(index)
                onMiddleClicked: Tasks.launchNew(index)

                // Il compositor fa volare qui le finestre ridotte a icona.
                function reportGeometry() {
                    if (windowCount > 0) {
                        const p = mapToItem(null, 0, 0)
                        Tasks.setButtonGeometry(index, root, Qt.rect(p.x, p.y, width, height))
                    }
                }
                onXChanged: reportGeometry()
                onWindowCountChanged: reportGeometry()
                Connections {
                    target: buttons
                    function onXChanged() { task.reportGeometry() }
                }

                Image {
                    anchors.fill: parent
                    source: "image://icon/" + encodeURIComponent(task.iconName)
                    sourceSize: Qt.size(52, 52)
                    smooth: true
                    mipmap: true
                }
            }
        }
    }

    // Area di notifica: le icone delle app (Telegram, Discord, Steam...),
    // a sinistra dell'orologio come su Windows.
    Row {
        id: tray
        anchors { right: clock.left; top: parent.top; bottom: parent.bottom; rightMargin: 4 }

        Repeater {
            model: Tray

            delegate: Item {
                id: trayItem
                required property int index
                required property string icon
                required property string title
                required property bool attention
                width: icon !== "" ? 32 : 0
                height: tray.height
                visible: icon !== ""

                Rectangle {
                    anchors { fill: parent; topMargin: 8; bottomMargin: 8 }
                    radius: Theme.radiusSmall
                    color: trayMouse.pressed ? Theme.pressed : Theme.hover
                    opacity: trayMouse.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                }
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    source: trayItem.icon
                    sourceSize: Qt.size(36, 36)
                    smooth: true
                    mipmap: true
                }
                // Chiede attenzione (messaggi non letti): un puntino.
                Rectangle {
                    visible: trayItem.attention
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.accent
                    anchors { right: parent.right; top: parent.top; rightMargin: 6; topMargin: 12 }
                }

                MouseArea {
                    id: trayMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onClicked: mouse => {
                        const center = trayItem.mapToItem(null, trayItem.width / 2, 0).x
                        if (mouse.button === Qt.RightButton) {
                            Tray.requestMenu(trayItem.index, center)
                        } else if (mouse.button === Qt.MiddleButton) {
                            Tray.secondaryActivate(trayItem.index)
                        } else {
                            Tray.activate(trayItem.index, center)
                        }
                    }
                    onWheel: wheel => Tray.scroll(trayItem.index, wheel.angleDelta.y)
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
