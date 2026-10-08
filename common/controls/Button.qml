// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Windows 11's button: standard (gray) or accent. Also works from the keyboard
// (Space, Enter) when it has the focus, with a focus ring.
Rectangle {
    id: button
    property string text
    property bool accent: false
    property bool usable: true
    signal clicked()

    implicitWidth: Math.max(96, label.implicitWidth + 24)
    implicitHeight: 32
    radius: Theme.radius
    opacity: usable ? 1 : 0.45
    color: accent ? (mouse.containsMouse && usable ? Theme.accentFillHover : Theme.accentFill)
                  : (mouse.pressed ? Theme.controlPressed : mouse.containsMouse ? Theme.controlHover : Theme.control)
    border.width: 1
    border.color: accent ? Qt.rgba(0, 0, 0, 0.1) : Theme.controlStroke
    activeFocusOnTab: usable

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (usable) clicked()
    Keys.onSpacePressed: if (usable) clicked()
    Keys.onReturnPressed: if (usable) clicked()
    Keys.onEnterPressed: if (usable) clicked()

    Text {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: button.accent ? Theme.accentText : Theme.text
        font.pixelSize: Theme.fontBody
    }
    // Keyboard focus: a ring around it, like Windows.
    Rectangle {
        visible: button.activeFocus
        anchors.fill: parent
        anchors.margins: -3
        radius: button.radius + 3
        color: "transparent"
        border.width: 2
        border.color: Theme.text
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.usable
        onClicked: button.clicked()
    }
}
