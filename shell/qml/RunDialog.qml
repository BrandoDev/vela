import QtQuick
import QtQuick.Dialogs

// "Esegui" (Win+R, o dal menu Win+X), come in Windows: in basso a sinistra,
// apre un programma, una cartella, un documento o un indirizzo.
Window {
    id: root
    objectName: "runDialog"
    visible: false
    width: 420
    height: 196
    color: "transparent"

    property bool browsing: false // la finestra "Sfoglia" ha la tastiera: non si chiude
    property int historyIndex: -1

    function open() {
        field.text = System.runHistory()[0] || ""
        field.selectAll()
        historyIndex = -1
        visible = true
        field.forceActiveFocus()
    }
    function close() {
        visible = false
    }
    function accept() {
        if (System.run(field.text)) {
            close()
        }
    }

    // In basso a sinistra, sopra la taskbar: da coordinate sue a quelle dello schermo.
    function screenPoint(item, x, y) {
        const p = item.mapToItem(null, x, y)
        return Qt.point(12 + p.x, Screen.height - Theme.taskbarHeight - 12 - root.height + p.y)
    }

    Connections {
        target: Shell
        function onRunRequested() {
            root.open()
        }
    }

    // Chiuso il menu, la tastiera torna qui; se invece è andata altrove (una
    // finestra nuova, un clic), si chiude anche questo pannello.
    Connections {
        target: Menus
        function onIsOpenChanged() {
            if (!Menus.isOpen && root.visible) {
                focusCheck.restart()
            }
        }
    }
    Timer {
        id: focusCheck
        interval: 200
        onTriggered: {
            if (!root.active && root.visible && !Menus.isOpen) {
                root.close()
            }
        }
    }

    onActiveChanged: {
        if (!active && visible && !browsing && !Menus.isOpen) {
            close()
        }
    }

    FileDialog {
        id: browse
        title: "Sfoglia"
        onAccepted: {
            root.browsing = false
            field.text = selectedFile.toString().replace(/^file:\/\//, "")
            field.forceActiveFocus()
        }
        onRejected: root.browsing = false
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke

        Image {
            x: 20
            y: 20
            width: 32
            height: 32
            source: "image://icon/system-run"
            sourceSize: Qt.size(width, height)
        }
        Text {
            x: 64
            y: 18
            width: parent.width - x - 20
            text: "Digita il nome di un programma, cartella, documento o risorsa Internet e Vela lo aprirà."
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        Text {
            x: 20
            y: 84
            text: "Apri:"
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
        Rectangle {
            id: fieldBox
            x: 64
            y: 76
            width: parent.width - x - 20
            height: 32
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            border.width: 1
            border.color: field.activeFocus ? Theme.accent : Theme.stroke

            MenuTextField {
                id: field
                anchors { left: parent.left; right: historyButton.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                mapToScreen: (x, y) => root.screenPoint(field, x, y)
                Keys.onEscapePressed: root.close()
                Keys.onReturnPressed: root.accept()
                Keys.onEnterPressed: root.accept()
                // Su e giù scorrono i comandi già usati, come l'elenco di Windows.
                Keys.onUpPressed: root.step(-1)
                Keys.onDownPressed: root.step(1)
            }
            // L'elenco dei comandi usati.
            Item {
                id: historyButton
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 28
                Text {
                    anchors.centerIn: parent
                    text: "⌄"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontNormal
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const p = root.screenPoint(fieldBox, 0, fieldBox.height + 2)
                        Menus.open(System.runHistory().map(command => ({
                            text: command.replace(/&/g, "&&"),
                            action: () => { field.text = command; field.forceActiveFocus() }
                        })), p.x, p.y, { minWidth: fieldBox.width })
                    }
                }
            }
        }

        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 20 }
            spacing: 8

            component DialogButton: Rectangle {
                id: button
                property string label
                property bool primary: false
                signal clicked()
                width: 96
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
                label: "OK"
                primary: true
                opacity: field.text.trim().length > 0 ? 1 : 0.5
                onClicked: root.accept()
            }
            DialogButton {
                label: "Annulla"
                onClicked: root.close()
            }
            DialogButton {
                label: "Sfoglia..."
                onClicked: {
                    root.browsing = true
                    browse.open()
                }
            }
        }
    }

    function step(direction) {
        const history = System.runHistory()
        if (history.length === 0) {
            return
        }
        historyIndex = Math.max(0, Math.min(history.length - 1, historyIndex + direction))
        field.text = history[historyIndex]
    }
}
