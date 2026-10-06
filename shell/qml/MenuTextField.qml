// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Un campo di testo della shell con il menu del tasto destro di Windows
// (Annulla, Taglia, Copia, Incolla, Elimina, Seleziona tutto), anche con
// il tasto Menu o Maiusc+F10.
TextInput {
    id: input

    // Da coordinate del campo a coordinate dello schermo: ogni finestra
    // della shell sa dove si trova.
    property var mapToScreen: (x, y) => input.mapToItem(null, x, y)

    color: Theme.text
    selectionColor: Theme.accent
    font.pixelSize: Theme.fontNormal
    selectByMouse: true
    persistentSelection: true // il menu prende la tastiera: la selezione deve restare
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
