// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// The desktop name, in the center of the output for a moment, when switching
// desktops (Win+Ctrl+arrows), like Windows 11.
Window {
    id: root
    visible: false
    width: label.implicitWidth + 64
    height: 64
    color: "transparent"

    property int last: -1 // not a binding: it's compared with the new one
    Component.onCompleted: last = Desktops.current

    Connections {
        target: Desktops
        function onChanged() {
            if (Desktops.current !== root.last) {
                root.last = Desktops.current
                // Task View already shows the desktops.
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
