// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// La riga in fondo, come Windows: quanti elementi, quanti selezionati e
// quanto pesano; a destra Dettagli e Icone grandi.
Item {
    id: bar
    property var model: null
    property string viewMode: "details"
    property string message // per le pagine senza file (Home, Questo PC)
    signal viewRequested(string mode)
    height: 28

    function plural(n, one, many) {
        return n === 1 ? "1 " + one : n + " " + many
    }

    Text {
        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
        color: Theme.text
        font.pixelSize: Theme.fontSmall
        text: {
            if (!bar.model) return bar.message
            let s = bar.plural(bar.model.count, "elemento", "elementi")
            if (bar.model.selectionCount > 0) {
                s += "      " + bar.plural(bar.model.selectionCount, "elemento selezionato", "elementi selezionati")
                if (bar.model.selectionSize > 0) s += "  " + Ops.formatSize(bar.model.selectionSize)
            }
            if (bar.model.loading) s += "      Caricamento..."
            return s
        }
    }
    Row {
        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
        visible: bar.model !== null
        spacing: 2
        ToolButton {
            width: 28
            height: 24
            icon: "view-list-details"
            checked: bar.viewMode === "details"
            onClicked: bar.viewRequested("details")
        }
        ToolButton {
            width: 28
            height: 24
            icon: "view-list-icons"
            checked: bar.viewMode === "large"
            onClicked: bar.viewRequested("large")
        }
    }
}
