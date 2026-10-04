import QtQuick
import QtQuick.Controls.Basic as C

// La casella di testo di Windows 11: la riga d'accento in basso quando ha il fuoco.
C.TextField {
    id: field
    implicitWidth: 240
    implicitHeight: 32
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accent
    selectedTextColor: "white"
    font.pixelSize: Theme.fontBody
    leftPadding: 11
    rightPadding: 11
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: Theme.radius
        color: field.activeFocus ? Qt.rgba(0.12, 0.12, 0.12, 0.7) : field.hovered ? Theme.controlHover : Theme.control
        border.width: 1
        border.color: Theme.controlStroke
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 1; rightMargin: 1 }
            height: field.activeFocus ? 2 : 1
            radius: 1
            color: field.activeFocus ? Theme.accentFill : Theme.controlStrokeStrong
            opacity: field.activeFocus ? 1 : 0.6
        }
    }
}
