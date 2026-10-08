// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// A Windows 11 button: standard (gray) or accent.
Rectangle {
    id: button
    property string text
    property string icon
    property bool accent: false
    property bool usable: true
    property bool subtle: false // no background, like the cards' "..." buttons
    signal clicked()

    implicitWidth: Math.max(subtle && text === "" ? 32 : 96, content.width + 24)
    width: implicitWidth
    height: 32
    radius: Theme.radius
    opacity: usable ? 1 : 0.45
    color: subtle ? (mouse.pressed ? Theme.subtlePressed : mouse.containsMouse ? Theme.subtleHover : "transparent")
         : accent ? (mouse.containsMouse ? Theme.accentFillHover : Theme.accentFill)
         : (mouse.pressed ? Theme.controlPressed : mouse.containsMouse ? Theme.controlHover : Theme.control)
    border.width: subtle ? 0 : 1
    border.color: accent ? Qt.rgba(0, 0, 0, 0.1) : Theme.controlStroke

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
            color: button.accent ? Theme.accentText : Theme.text
            font.pixelSize: Theme.fontBody
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.usable
        onClicked: button.clicked()
    }
}
