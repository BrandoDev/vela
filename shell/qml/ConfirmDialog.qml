// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import Vela.Controls

// A question before an action that can't be undone (such as emptying the
// Recycle Bin), like Windows confirmation dialogs. Opened with
// Menus.confirm(); Enter confirms, Esc gives up.
Window {
    id: root
    visible: false
    width: 420
    height: 170
    color: "transparent"

    property string title
    property string text
    property string yesText: qsTr("Yes")
    property var action: null

    Connections {
        target: Menus
        function onConfirmRequested(title, text, yesText, action) {
            root.title = title
            root.text = text
            root.yesText = yesText || qsTr("Yes")
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
        radius: Theme.radiusOverlay
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
            font.pixelSize: Theme.fontBody + 2
            font.weight: Font.DemiBold
        }
        Text {
            x: 24
            y: 52
            width: parent.width - 48
            text: root.text
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }

        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 20 }
            spacing: 8

            Button {
                text: root.yesText
                accent: true
                onClicked: root.answer(true)
            }
            Button {
                text: qsTr("No")
                onClicked: root.answer(false)
            }
        }
    }
}
