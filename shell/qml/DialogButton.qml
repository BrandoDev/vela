import QtQuick

// Un pulsante delle finestre di dialogo della shell, come Windows 11: quello
// principale pieno dell'accento.
Rectangle {
    id: button
    property string label
    property bool primary: false
    property bool usable: true
    signal clicked()
    width: Math.max(96, text.implicitWidth + 32)
    height: 32
    radius: Theme.radiusSmall
    opacity: usable ? 1 : 0.5
    color: primary ? (mouse.pressed ? Qt.darker(Theme.accent, 1.2) : mouse.containsMouse ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
                   : (mouse.pressed ? Theme.pressed : mouse.containsMouse ? Theme.hover : Theme.surfaceRaised)
    border.width: primary ? 0 : 1
    border.color: Theme.stroke
    Text {
        id: text
        anchors.centerIn: parent
        text: button.label
        color: button.primary ? "white" : Theme.text
        font.pixelSize: Theme.fontNormal
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.usable
        onClicked: button.clicked()
    }
}
