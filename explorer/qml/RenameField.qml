// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Il nome da modificare sul posto (F2, Rinomina), come Windows: si
// seleziona il nome senza l'estensione; Invio conferma, Esc annulla.
Rectangle {
    id: field
    property string path
    property string name
    property bool isDir: false
    property bool centered: false
    signal done(string newName)
    signal cancelled()

    height: input.contentHeight + 8
    radius: Theme.radiusSmall
    color: Theme.field
    border.width: 1
    border.color: Theme.accent

    function start() {
        input.text = name
        input.forceActiveFocus()
        const dot = name.lastIndexOf(".")
        if (!isDir && dot > 0) {
            input.select(0, dot)
        } else {
            input.selectAll()
        }
    }
    Component.onCompleted: start()

    TextInput {
        id: input
        anchors { fill: parent; leftMargin: 4; rightMargin: 4; topMargin: 4 }
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: "white"
        font.pixelSize: Theme.fontNormal
        selectByMouse: true
        wrapMode: field.centered ? TextInput.WrapAnywhere : TextInput.NoWrap
        horizontalAlignment: field.centered ? TextInput.AlignHCenter : TextInput.AlignLeft
        clip: true
        Keys.onReturnPressed: field.done(text)
        Keys.onEnterPressed: field.done(text)
        Keys.onEscapePressed: field.cancelled()
        onActiveFocusChanged: if (!activeFocus && field.visible) field.done(text)
    }
}
