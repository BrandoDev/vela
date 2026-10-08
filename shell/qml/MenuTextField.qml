// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import Vela.Controls

// A shell text field with the Windows right-click menu (Undo, Cut, Copy,
// Paste, Delete, Select all), also with the Menu key or Shift+F10.
TextInput {
    id: input

    // From field to output coordinates: every shell window knows where it is.
    property var mapToScreen: (x, y) => input.mapToItem(null, x, y)

    color: Theme.text
    selectionColor: Theme.accent
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    persistentSelection: true // the menu takes the keyboard: the selection must stay
    clip: true

    function openMenu(x, y, keyboard) {
        const p = mapToScreen(x, y)
        Menus.open(Menus.textEntries(input), p.x, p.y, { keyboard: keyboard })
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            const r = input.cursorRectangle
            openMenu(r.x, r.y + r.height, true)
            event.accepted = true
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        cursorShape: Qt.IBeamCursor
        onPressed: mouse => {
            input.forceActiveFocus()
            input.openMenu(mouse.x, mouse.y, false)
        }
    }
}
