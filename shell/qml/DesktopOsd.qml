import QtQuick

// Il nome del desktop, al centro dello schermo per un attimo, quando si
// cambia desktop (Win+Ctrl+frecce), come Windows 11.
Window {
    id: root
    objectName: "desktopOsd"
    visible: false
    width: label.implicitWidth + 64
    height: 64
    color: "transparent"

    property int last: -1 // non un binding: va confrontato con quello nuovo
    Component.onCompleted: last = Desktops.current

    Connections {
        target: Desktops
        function onChanged() {
            if (Desktops.current !== root.last) {
                root.last = Desktops.current
                // La Visualizzazione attività mostra già i desktop.
                if (!Menus.isOpen && !Menus.taskViewOpen) {
                    root.show()
                }
            }
        }
    }

    function show() {
        visible = true
        Effects.setBlur(root, [Qt.rect(0, 0, width, height)])
        pill.opacity = 0
        fade.stop()
        appear.restart()
        hideTimer.restart()
    }

    NumberAnimation { id: appear; target: pill; property: "opacity"; to: 1; duration: Theme.fast }
    SequentialAnimation {
        id: fade
        NumberAnimation { target: pill; property: "opacity"; to: 0; duration: Theme.normal }
        ScriptAction { script: root.visible = false }
    }
    Timer { id: hideTimer; interval: 900; onTriggered: fade.start() }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
        Text {
            id: label
            anchors.centerIn: parent
            text: Desktops.names[Desktops.current] || ""
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
    }
}
