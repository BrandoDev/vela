// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls.Basic as C

// Il contenuto di una pagina: scorre, largo al massimo 1000 px come su
// Windows. I figli vanno in colonna.
Flickable {
    id: page
    default property alias content: column.data
    property alias spacing: column.spacing
    contentWidth: width
    contentHeight: column.height + 40
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    C.ScrollBar.vertical: C.ScrollBar {
        policy: page.contentHeight > page.height ? C.ScrollBar.AsNeeded : C.ScrollBar.AlwaysOff
    }

    Column {
        id: column
        x: 0
        width: Math.min(page.width - 24, 1000)
        spacing: 4
    }
}
