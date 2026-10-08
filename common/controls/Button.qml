// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Windows 11's button: standard (gray), accent, or subtle (no background,
// like the cards' "..." buttons), with text, an icon or both. Also works from
// the keyboard (Space, Enter) when it has the focus, with a focus ring.
Rectangle {
    id: button
    property string text
    property string icon
    property bool accent: false
    property bool subtle: false
    property bool usable: true
    signal clicked()

    implicitWidth: Math.max(subtle && text === "" ? 32 : 96, content.implicitWidth + 24)
    implicitHeight: 32
    radius: Theme.radius
    opacity: usable ? 1 : 0.45
    color: subtle ? (mouse.pressed ? Theme.subtlePressed : mouse.containsMouse ? Theme.subtleHover : "transparent")
         : accent ? (mouse.containsMouse && usable ? Theme.accentFillHover : Theme.accentFill)
         : (mouse.pressed ? Theme.controlPressed : mouse.containsMouse ? Theme.controlHover : Theme.control)
    border.width: subtle ? 0 : 1
    border.color: accent ? Qt.rgba(0, 0, 0, 0.1) : Theme.controlStroke
    activeFocusOnTab: usable

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (usable) clicked()
    Keys.onSpacePressed: if (usable) clicked()
    Keys.onReturnPressed: if (usable) clicked()
    Keys.onEnterPressed: if (usable) clicked()

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
