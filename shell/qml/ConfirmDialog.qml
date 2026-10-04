import QtQuick

// Una domanda prima di un'azione che non si può annullare (es. svuotare il
// Cestino), come le finestre di conferma di Windows. Si apre con
// Menus.confirm(); Invio conferma, Esc rinuncia.
Window {
    id: root
    objectName: "confirmDialog"
    visible: false
    width: 420
    height: 170
    color: "transparent"

    property string title
    property string text
    property string yesText: "Sì"
    property var action: null

    Connections {
        target: Menus
        function onConfirmRequested(title, text, yesText, action) {
            root.title = title
            root.text = text
            root.yesText = yesText || "Sì"
            root.action = action
            root.visible = true
            panel.forceActiveFocus()
        }
    }

    function answer(yes) {
        const run = yes ? action : null
        visible = false
        action = null
        if (run) {
            Qt.callLater(run)
        }
    }

    onActiveChanged: {
        if (!active && visible) {
            answer(false)
        }
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke
        focus: true
        Keys.onEscapePressed: root.answer(false)
        Keys.onReturnPressed: root.answer(true)
        Keys.onEnterPressed: root.answer(true)

        Text {
            x: 24
            y: 20
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontNormal + 2
            font.weight: Font.DemiBold
        }
        Text {
            x: 24
            y: 52
            width: parent.width - 48
            text: root.text
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            wrapMode: Text.Wrap
        }

        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 20 }
            spacing: 8

            component DialogButton: Rectangle {
                id: button
                property string label
                property bool primary: false
                signal clicked()
                width: 110
                height: 32
                radius: Theme.radiusSmall
                color: primary ? (mouse.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent)
                               : (mouse.pressed ? Theme.pressed : mouse.containsMouse ? Theme.hover : Theme.surfaceRaised)
                border.width: primary ? 0 : 1
                border.color: Theme.stroke
                Text {
                    anchors.centerIn: parent
                    text: button.label
                    color: button.primary ? "white" : Theme.text
                    font.pixelSize: Theme.fontNormal
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: button.clicked()
                }
            }

            DialogButton {
                label: root.yesText
                primary: true
                onClicked: root.answer(true)
            }
            DialogButton {
                label: "No"
                onClicked: root.answer(false)
            }
        }
    }
}
