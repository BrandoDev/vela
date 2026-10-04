import QtQuick

// Un pulsante della barra dei comandi o della barra degli indirizzi, come
// Windows 11: senza fondo finché non ci si passa sopra; icona, testo
// facoltativo, freccia se apre un menu.
Rectangle {
    id: button
    property string icon
    property string text
    property bool usable: true
    property bool menu: false // con la freccetta: apre un menu
    property bool checked: false
    property string tooltip
    signal clicked()

    implicitWidth: content.width + (text !== "" ? 20 : 16)
    implicitHeight: 32
    width: implicitWidth
    height: implicitHeight
    radius: Theme.radiusSmall
    color: mouse.pressed && usable ? Theme.pressed : (mouse.containsMouse && usable) || checked ? Theme.hover : "transparent"
    opacity: usable ? 1 : 0.4

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 8
        Image {
            visible: button.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: button.icon !== "" ? Theme.icons + encodeURIComponent(button.icon) : ""
            sourceSize: Qt.size(width, height)
        }
        Text {
            visible: button.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
        Text {
            visible: button.menu
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: Theme.textDim
            font.pixelSize: 12
            topPadding: -5
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.usable
        onClicked: { tipTimer.stop(); tip.visible = false; button.clicked() }
        onContainsMouseChanged: {
            if (containsMouse && button.tooltip !== "") {
                tipTimer.restart()
            } else {
                tipTimer.stop()
                tip.visible = false
            }
        }
    }

    // Il suggerimento, come Windows: dopo un attimo fermo sopra, sotto il pulsante.
    Timer {
        id: tipTimer
        interval: 600
        onTriggered: {
            const host = button.Window.window ? button.Window.window.contentItem : null
            if (!host) return
            tip.parent = host
            const p = button.mapToItem(host, button.width / 2, button.height + 6)
            tip.x = Math.max(4, Math.min(host.width - tip.width - 4, p.x - tip.width / 2))
            tip.y = p.y
            tip.visible = true
        }
    }
    Rectangle {
        id: tip
        visible: false
        z: 1000
        width: tipText.implicitWidth + 16
        height: tipText.implicitHeight + 10
        radius: Theme.radiusSmall
        color: Theme.popup
        border.width: 1
        border.color: Theme.stroke
        Text {
            id: tipText
            anchors.centerIn: parent
            text: button.tooltip
            color: Theme.text
            font.pixelSize: Theme.fontSmall
        }
    }
}
