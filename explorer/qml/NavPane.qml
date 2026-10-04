import QtQuick

// Il riquadro di navigazione a sinistra, come Windows 11: Home, le cartelle
// di Accesso rapido (con la puntina), Questo PC con le unità (anche quelle
// non montate, che si montano aprendole), il Cestino.
// Si possono lasciare file su una voce per spostarli o copiarli lì.
Flickable {
    id: pane
    property string current // la posizione aperta, per evidenziarla
    signal navigate(string location)
    signal openInNewTab(string location)
    signal openVolume(string volume) // un'unità non montata: si monta e si apre
    signal contextMenu(var entries, real x, real y)

    contentHeight: column.height + 16
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    component Entry: Rectangle {
        id: entry
        property string icon
        property string label
        property string location
        property bool pinned: false
        property string volume: "" // unità non montata (udisks)
        property string device: "" // unità rimovibile montata: si può espellere
        property bool droppable: location.startsWith("/")
        property int indent: 0
        readonly property bool selected: pane.current === location
        width: column.width
        height: 32
        radius: Theme.radiusSmall
        color: drop.containsDrag ? Theme.selectionHover : selected ? Theme.selection : mouse.containsMouse ? Theme.hover : "transparent"

        Rectangle {
            visible: entry.selected
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 16
            radius: 1.5
            color: Theme.accentLight
        }
        Image {
            x: 12 + entry.indent
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: "image://fileicon/" + encodeURIComponent(entry.icon)
            sourceSize: Qt.size(width, height)
        }
        Text {
            x: 38 + entry.indent
            width: parent.width - x - (entry.pinned ? 28 : 8)
            anchors.verticalCenter: parent.verticalCenter
            text: entry.label
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            elide: Text.ElideRight
        }
        Text {
            visible: entry.pinned
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: "\u{1F4CC}"
            font.pixelSize: 10
            opacity: 0.55
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.MiddleButton) {
                    pane.openInNewTab(entry.location)
                } else if (mouse.button === Qt.RightButton) {
                    const p = entry.mapToItem(null, mouse.x, mouse.y)
                    pane.contextMenu(pane.entryMenu(entry), p.x, p.y)
                } else if (entry.volume !== "") {
                    pane.openVolume(entry.volume)
                } else {
                    pane.navigate(entry.location)
                }
            }
        }
        DropArea {
            id: drop
            anchors.fill: parent
            enabled: entry.droppable
            onEntered: drag => { if (!drag.hasUrls) drag.accepted = false }
            onDropped: drop => {
                Ops.drop(drop.urls.map(u => u.toString()), entry.location, 0)
                drop.accept(Qt.MoveAction)
            }
        }
    }

    // Il menu di una voce: Apri, Apri in una nuova scheda, la puntina, Proprietà.
    function entryMenu(entry) {
        const loc = entry.location
        if (entry.volume !== "") {
            return [{ text: "&Apri", icon: "document-open", action: () => pane.openVolume(entry.volume) }]
        }
        const entries = [
            { text: "&Apri", icon: "document-open", action: () => pane.navigate(loc) },
            { text: "Apri in una nuova &scheda", icon: "tab-new", action: () => pane.openInNewTab(loc) },
            { text: "Apri in una nuova &finestra", icon: "window-new", action: () => Ops.newWindow(loc) }
        ]
        if (loc.startsWith("/") && loc !== Places.trash) {
            entries.push({ separator: true })
            entries.push(Places.isPinned(loc)
                ? { text: "&Rimuovi da Accesso rapido", icon: "window-unpin", action: () => Places.unpin(loc) }
                : { text: "Aggiungi ad &Accesso rapido", icon: "window-pin", action: () => Places.pin(loc) })
            entries.push({ text: "Apri in &Terminale", icon: "utilities-terminal", action: () => System.openTerminal(loc) })
            if (entry.device !== "") {
                entries.push({ text: "&Espelli", icon: "media-eject", action: () => Places.eject(entry.device) })
            }
            entries.push({ separator: true })
            entries.push({ text: "P&roprietà", icon: "document-properties", action: () => Ops.showProperties([loc]) })
        }
        if (loc === Places.trash) {
            entries.push({ separator: true })
            entries.push({ text: "&Svuota Cestino", icon: "trash-empty", action: () => Ops.emptyTrash() })
        }
        return entries
    }

    Column {
        id: column
        x: 8
        y: 8
        width: pane.width - 16
        spacing: 2

        Entry { icon: "go-home"; label: "Home"; location: "home:" }
        Item { width: 1; height: 8 }
        Repeater {
            model: Places.quickAccess
            delegate: Entry {
                required property var modelData
                icon: modelData.icon
                label: modelData.name
                location: modelData.path
                pinned: true
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        Entry { icon: "computer"; label: "Questo PC"; location: "thispc:" }
        Repeater {
            model: Places.drives
            delegate: Entry {
                required property var modelData
                icon: modelData.icon
                label: modelData.name
                location: modelData.path
                volume: modelData.volume || ""
                device: modelData.mounted && modelData.removable ? modelData.device : ""
                opacity: modelData.mounted ? 1 : 0.7
                indent: 16
            }
        }
        Entry { icon: "user-home"; label: Places.userName; location: Places.home; indent: 16 }
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        Entry { icon: "user-trash"; label: "Cestino"; location: Places.trash }
    }
}
