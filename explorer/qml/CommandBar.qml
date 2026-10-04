import QtQuick

// La barra dei comandi di Windows 11: Nuovo; Taglia, Copia, Incolla,
// Rinomina, Condividi, Elimina; Ordina e Visualizza; "…" con il resto. Nel
// Cestino: Svuota Cestino e Ripristina.
Item {
    id: bar
    property var tab
    height: 48

    readonly property bool hasSelection: tab.model && tab.model.selectionCount > 0
    readonly property bool writable: tab.isFolder && !tab.isTrash

    function openMenu(entries, item) {
        const p = item.mapToItem(null, 0, item.height)
        tab.menus.open(entries, p.x, p.y + 2)
    }

    Row {
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        ToolButton {
            id: newButton
            visible: !bar.tab.isTrash
            icon: "list-add"
            text: "Nuovo"
            menu: true
            usable: bar.writable
            onClicked: bar.openMenu(bar.tab.newEntries(), newButton)
        }
        Rectangle { visible: !bar.tab.isTrash; width: 1; height: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.divider }
        Item { visible: !bar.tab.isTrash; width: 4; height: 1 }

        ToolButton { visible: !bar.tab.isTrash; icon: "edit-cut"; tooltip: "Taglia (Ctrl+X)"; usable: bar.hasSelection && bar.writable; onClicked: bar.tab.cutSelection() }
        ToolButton { visible: !bar.tab.isTrash; icon: "edit-copy"; tooltip: "Copia (Ctrl+C)"; usable: bar.hasSelection; onClicked: bar.tab.copySelection() }
        ToolButton { visible: !bar.tab.isTrash; icon: "edit-paste"; tooltip: "Incolla (Ctrl+V)"; usable: Ops.canPaste && bar.writable; onClicked: bar.tab.paste() }
        ToolButton { visible: !bar.tab.isTrash; icon: "edit-rename"; tooltip: "Rinomina (F2)"; usable: bar.tab.model && bar.tab.model.selectionCount === 1 && bar.writable; onClicked: bar.tab.renameSelection() }
        ToolButton { visible: !bar.tab.isTrash; icon: "document-share"; tooltip: "Condividi"; usable: false }
        ToolButton { visible: !bar.tab.isTrash; icon: "edit-delete"; tooltip: "Elimina (Canc)"; usable: bar.hasSelection && bar.writable; onClicked: bar.tab.deleteSelection(false) }

        // Nel Cestino.
        ToolButton { visible: bar.tab.isTrash; icon: "trash-empty"; text: "Svuota Cestino"; usable: bar.tab.model && bar.tab.model.count > 0; onClicked: bar.tab.askEmptyTrash() }
        ToolButton { visible: bar.tab.isTrash; icon: "edit-undo"; text: "Ripristina gli elementi selezionati"; usable: bar.hasSelection; onClicked: Ops.restoreFromTrash(bar.tab.model.selectedPaths()) }

        Item { width: 4; height: 1 }
        Rectangle { width: 1; height: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.divider }
        Item { width: 4; height: 1 }

        ToolButton {
            id: sortButton
            icon: "view-sort"
            text: "Ordina"
            menu: true
            usable: bar.tab.isFolder
            onClicked: bar.openMenu(bar.tab.sortEntries(), sortButton)
        }
        ToolButton {
            id: viewButton
            icon: "view-list-icons"
            text: "Visualizza"
            menu: true
            usable: bar.tab.isFolder
            onClicked: bar.openMenu(bar.tab.viewEntries(), viewButton)
        }
        Item { width: 4; height: 1 }
        Rectangle { width: 1; height: 24; anchors.verticalCenter: parent.verticalCenter; color: Theme.divider }
        Item { width: 4; height: 1 }
        ToolButton {
            id: moreButton
            icon: "view-more-horizontal-symbolic"
            tooltip: "Visualizza altro"
            usable: bar.tab.isFolder
            onClicked: bar.openMenu(bar.tab.moreEntries(), moreButton)
        }
    }
}
