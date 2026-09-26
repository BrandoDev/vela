import QtQuick

// Pulsante della taskbar: sfondo che sfuma al passaggio del mouse e un
// leggero "rimbalzo" quando lo premi. Sotto l'icona, come su Windows 11, un
// trattino grigio se l'app è aperta, più lungo e colorato se è quella attiva.
Item {
    id: root

    property string tooltip: ""
    property bool active: false
    property bool running: false
    default property alias content: holder.data

    signal clicked()
    signal middleClicked()

    width: 44
    height: 40

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: mouse.pressed ? Theme.pressed : Theme.hover
        opacity: mouse.containsMouse || root.active ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        }
    }

    Item {
        id: holder
        anchors.centerIn: parent
        width: 26
        height: 26
        scale: mouse.pressed ? 0.84 : 1

        Behavior on scale {
            NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        }
    }

    // Indicatore sotto l'icona: app aperta / attiva (o menu Start aperto)
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        height: 3
        radius: 1.5
        width: root.active ? 16 : root.running ? 6 : 0
        color: root.active ? Theme.accent : Theme.textDim
        opacity: root.active || root.running ? 1 : 0

        Behavior on width {
            NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        }
        Behavior on color {
            ColorAnimation { duration: Theme.fast }
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.fast }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        onClicked: mouse => mouse.button === Qt.MiddleButton ? root.middleClicked() : root.clicked()
    }
}
