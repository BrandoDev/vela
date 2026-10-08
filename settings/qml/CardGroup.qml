// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import Vela.Controls

// A group of cards with its title, like Windows 11 sections ("Scale & layout",
// "Related settings"...): cards slightly apart.
Column {
    id: group
    property string title
    default property alias cards: cardColumn.data
    width: parent ? parent.width : 0
    spacing: 0

    Text {
        visible: group.title !== ""
        text: group.title
        color: Theme.text
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
        topPadding: 20
        bottomPadding: 8
    }
    Column {
        id: cardColumn
        width: parent.width
        spacing: 4
    }
}
