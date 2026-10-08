// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Dialogs

// "Run" (Win+R, or from the Win+X menu), like in Windows: at the bottom left,
// opens a program, a folder, a document or an address.
Window {
    id: root
    visible: false
    width: 420
    height: 196
    color: "transparent"

    property bool browsing: false // the "Browse" window has the keyboard: it doesn't close
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

    // At the bottom left, above the taskbar: from its coordinates to the
    // output's.
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

    // Once the menu closes, the keyboard comes back here; if it went elsewhere
    // instead (a new window, a click), this panel closes too.
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
        title: qsTr("Browse")
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
            source: Theme.icons + "system-run"
            sourceSize: Qt.size(width, height)
        }
        Text {
            x: 64
            y: 18
            width: parent.width - x - 20
            text: qsTr("Type the name of a program, folder, document or Internet resource, and Vela will open it for you.")
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        Text {
            x: 20
            y: 84
            text: qsTr("Open:")
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
                // Up and down go through commands already used, like Windows'
                // list.
                Keys.onUpPressed: root.step(-1)
                Keys.onDownPressed: root.step(1)
            }
            // The list of used commands.
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
                label: qsTr("Cancel")
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
