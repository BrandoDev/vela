import QtQuick

// La Visualizzazione attività (Win+Tab, o il pulsante sulla taskbar), come
// Windows 11: in alto le finestre del desktop in uso, con anteprima; in
// basso i desktop e "Nuovo desktop". Passando su un desktop se ne vedono le
// finestre; una finestra trascinata su un desktop ci si sposta. Tasto
// destro: i menu di docs/renderer.md §14.10.
Window {
    id: root
    objectName: "taskView"
    visible: false
    color: "transparent"

    // Il desktop di cui si vedono le finestre: quello in uso, o quello sotto il mouse.
    property int shown: Desktops.current
    property var windows: [] // [{id, title, icon, workspace}]
    property int revision: 0 // cresce a ogni anteprima pronta
    property var aspects: ({}) // id -> larghezza/altezza dell'anteprima
    property string dragging: "" // la finestra che si sta trascinando

    function open() {
        if (visible) {
            close()
            return
        }
        shown = Desktops.current
        Menus.taskViewOpen = true
        refresh()
        Capture.capture(windows.map(w => w.id))
        visible = true
        content.opacity = 0
        content.scale = 1.03
        appear.restart()
        content.forceActiveFocus()
        updateBlur()
    }
    // Tutto lo sfondo sfocato; la misura arriva dal compositor dopo la prima apertura.
    function updateBlur() {
        if (visible) {
            Effects.setBlur(root, [Qt.rect(0, 0, width, height)])
        }
    }
    onWidthChanged: updateBlur()
    onHeightChanged: updateBlur()
    function close() {
        visible = false
        renaming = -1
        // Il desktop scelto qui arriva un attimo dopo: niente nome a metà schermo.
        releaseFlag.restart()
    }
    Timer { id: releaseFlag; interval: 600; onTriggered: if (!root.visible) Menus.taskViewOpen = false }
    function refresh() {
        windows = Capture.windowList().map(w => ({
            id: w.id, title: w.title, icon: Apps.iconForAppId(w.appId), appId: w.appId,
            workspace: Desktops.workspaceOf(w.id)
        })).filter(w => w.workspace !== -2)
    }
    // Le finestre del desktop mostrato (più quelle su tutti i desktop).
    readonly property var shownWindows: windows.filter(w => w.workspace === shown || w.workspace === -1)

    function activate(id) {
        Shell.windowAction(id, "activate")
        close()
    }

    Connections {
        target: Shell
        function onTaskViewRequested() { root.open() }
    }
    Connections {
        target: Desktops
        function onChanged() {
            if (root.visible) {
                root.refresh()
                if (root.shown >= Desktops.count) root.shown = Desktops.current
            }
        }
    }
    Connections {
        target: Capture
        function onThumbnailReady(identifier) {
            root.revision++
        }
    }
    onActiveChanged: {
        if (!active && visible && !Menus.isOpen) {
            close()
        }
    }

    ParallelAnimation {
        id: appear
        NumberAnimation { target: content; property: "opacity"; to: 1; duration: Theme.fast }
        NumberAnimation { target: content; property: "scale"; to: 1; duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
    }

    // Il fondo: lo sfondo sfocato, scurito. Un clic sul vuoto chiude.
    Rectangle {
        anchors.fill: parent
        color: Theme.backdrop
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: root.close()
        }
    }

    // Il menu del tasto destro, in coordinate dello schermo.
    function openMenu(entries, item, x, y) {
        const p = item.mapToItem(null, x, y)
        Menus.open(entries, p.x, p.y, { screen: root.screen ? root.screen.name : "" })
    }
    function windowMenu(w) {
        const moveTo = []
        for (let i = 0; i < Desktops.count; ++i) {
            if (i !== w.workspace) {
                const index = i
                moveTo.push({ text: Desktops.names[i].replace(/&/g, "&&"), action: () => Desktops.moveWindow(w.id, index) })
            }
        }
        moveTo.push({ separator: true })
        moveTo.push({ text: "&Nuovo desktop", icon: "list-add", action: () => Desktops.moveWindowToNew(w.id) })
        const appSticky = Desktops.stickyApps.indexOf(w.appId) >= 0
        return [
            { text: "Aggancia a &sinistra", icon: "window-snap-left", action: () => { Shell.windowAction(w.id, "snap-left"); root.close() } },
            { text: "Aggancia a &destra", icon: "window-snap-right", action: () => { Shell.windowAction(w.id, "snap-right"); root.close() } },
            { separator: true },
            { text: "S&posta in", enabled: w.workspace !== -1, children: moveTo },
            { text: "&Mostra questa finestra su tutti i desktop", checked: w.workspace === -1, action: () => Desktops.setWindowSticky(w.id, w.workspace !== -1) },
            { text: "Mostra &le finestre di questa app su tutti i desktop", checked: appSticky, enabled: w.appId !== "", action: () => Desktops.setAppSticky(w.id, !appSticky) },
            { separator: true },
            { text: "&Chiudi", icon: "window-close", action: () => Shell.windowAction(w.id, "close") }
        ]
    }
    property int renaming: -1
    function desktopMenu(index) {
        return [
            { text: "&Rinomina", icon: "edit-rename", action: () => root.renaming = index },
            { text: "Scegli &sfondo", icon: "preferences-desktop-wallpaper", action: () => { System.trigger("personalize"); root.close() } },
            { separator: true },
            { text: "Sposta a s&inistra", icon: "go-previous", enabled: index > 0, action: () => Desktops.move(index, index - 1) },
            { text: "Sposta a d&estra", icon: "go-next", enabled: index < Desktops.count - 1, action: () => Desktops.move(index, index + 1) },
            { separator: true },
            { text: "&Chiudi", icon: "window-close", enabled: Desktops.count > 1, action: () => Desktops.remove(index) }
        ]
    }

    Item {
        id: content
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: root.close()

        // --- le finestre ---
        Item {
            id: grid
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: strip.top; margins: 48; bottomMargin: 24 }
            z: root.dragging !== "" ? 2 : 0 // la finestra trascinata passa sopra i desktop

            readonly property int titleHeight: 36
            readonly property int gap: 24
            // Righe di anteprime della stessa altezza, la più grande che ci sta.
            readonly property var placement: {
                const items = root.shownWindows
                root.revision // le proporzioni cambiano quando arrivano le anteprime
                const aspect = items.map(w => Math.max(0.5, Math.min(2.6, root.aspects[w.id] || 1.6)))
                const maxHeight = 340
                for (let rows = 1; rows <= Math.max(1, items.length); ++rows) {
                    let h = Math.min(maxHeight, (height - gap * (rows - 1)) / rows - titleHeight)
                    const lines = [[]]
                    let w = 0
                    for (let i = 0; i < items.length; ++i) {
                        const iw = h * aspect[i]
                        if (w > 0 && w + gap + iw > width) {
                            lines.push([])
                            w = 0
                        }
                        lines[lines.length - 1].push(i)
                        w += (w > 0 ? gap : 0) + iw
                    }
                    if (lines.length <= rows || rows === items.length) {
                        // Una riga troppo larga (una finestra molto larga): si rimpicciolisce.
                        let widest = 0
                        for (const line of lines) {
                            widest = Math.max(widest, line.reduce((s, i) => s + h * aspect[i], 0) + gap * (line.length - 1))
                        }
                        if (widest > width) h *= width / widest
                        const totalHeight = lines.length * (h + titleHeight) + (lines.length - 1) * gap
                        const out = []
                        let y = (height - totalHeight) / 2
                        for (const line of lines) {
                            const lineWidth = line.reduce((s, i) => s + h * aspect[i], 0) + gap * (line.length - 1)
                            let x = (width - lineWidth) / 2
                            for (const i of line) {
                                out[i] = { x: x, y: y, width: h * aspect[i], height: h + titleHeight }
                                x += h * aspect[i] + gap
                            }
                            y += h + titleHeight + gap
                        }
                        return out
                    }
                }
                return []
            }

            Text {
                visible: root.shownWindows.length === 0
                anchors.centerIn: parent
                text: "Nessuna finestra aperta su questo desktop"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }

            Repeater {
                model: root.shownWindows
                delegate: Item {
                    id: card
                    required property var modelData
                    required property int index
                    readonly property var place: grid.placement[index] || { x: 0, y: 0, width: 0, height: 0 }
                    x: place.x
                    y: place.y
                    width: place.width
                    height: place.height
                    Behavior on x { enabled: root.dragging === ""; NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate } }
                    Behavior on y { enabled: root.dragging === ""; NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate } }

                    // Quello che si trascina: una copia che segue il mouse.
                    Rectangle {
                        id: frame
                        width: card.width
                        height: card.height
                        radius: Theme.radiusLarge
                        color: hover.hovered || cardMouse.drag.active ? Theme.cardHover : Theme.card
                        border.width: hover.hovered ? 2 : 1
                        border.color: hover.hovered ? Theme.accent : Theme.stroke
                        scale: cardMouse.drag.active ? 0.5 : 1
                        Behavior on scale { NumberAnimation { duration: Theme.fast } }
                        Drag.active: cardMouse.drag.active
                        Drag.keys: ["vela-window"]
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        property string windowId: card.modelData.id

                        Row {
                            x: 12
                            y: 10
                            width: parent.width - 52
                            spacing: 8
                            Image {
                                width: 16
                                height: 16
                                source: Theme.icons + encodeURIComponent(card.modelData.icon)
                                sourceSize: Qt.size(width, height)
                            }
                            Text {
                                width: parent.width - 24
                                text: card.modelData.title
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                        Image {
                            id: thumb
                            x: 8
                            y: grid.titleHeight
                            width: parent.width - 16
                            height: parent.height - grid.titleHeight - 8
                            source: root.revision > 0 ? "image://thumbnail/" + card.modelData.id + "/" + root.revision : ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: false
                            cache: false
                            onStatusChanged: {
                                if (status === Image.Ready && implicitHeight > 0) {
                                    const a = implicitWidth / implicitHeight
                                    if (Math.abs((root.aspects[card.modelData.id] || 0) - a) > 0.01) {
                                        const copy = Object.assign({}, root.aspects)
                                        copy[card.modelData.id] = a
                                        root.aspects = copy
                                    }
                                }
                            }
                        }
                        // Chiudi, al passaggio del mouse.
                        Rectangle {
                            visible: hover.hovered && !cardMouse.drag.active
                            anchors { right: parent.right; top: parent.top; margins: 6 }
                            width: 28
                            height: 24
                            radius: Theme.radiusSmall
                            color: closeMouse.containsMouse ? "#c42b1c" : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: "✕"
                                color: Theme.text
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: closeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: Shell.windowAction(card.modelData.id, "close")
                            }
                        }

                        HoverHandler { id: hover }
                        MouseArea {
                            id: cardMouse
                            property bool dragged: false
                            anchors.fill: parent
                            z: -1
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            drag.target: frame
                            drag.threshold: 8
                            onPressed: mouse => {
                                dragged = false
                                if (mouse.button === Qt.LeftButton) root.dragging = card.modelData.id
                            }
                            onPositionChanged: if (drag.active) dragged = true
                            onReleased: {
                                if (dragged && frame.Drag.target !== null) {
                                    frame.Drag.drop()
                                }
                                frame.x = 0
                                frame.y = 0
                                root.dragging = ""
                            }
                            onClicked: mouse => {
                                if (mouse.button === Qt.RightButton) {
                                    root.openMenu(root.windowMenu(card.modelData), frame, mouse.x, mouse.y)
                                } else if (!dragged) {
                                    root.activate(card.modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- i desktop ---
        Item {
            id: strip
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 196
            Rectangle {
                anchors.fill: parent
                color: Theme.footer
            }

            Row {
                id: desktops
                anchors.centerIn: parent
                spacing: 16
                readonly property real cardWidth: 208
                readonly property real cardHeight: Math.round(cardWidth * Screen.height / Math.max(1, Screen.width))

                Repeater {
                    model: Desktops.count
                    delegate: Column {
                        id: desk
                        required property int index
                        readonly property bool current: index === Desktops.current
                        spacing: 8

                        Rectangle {
                            width: desktops.cardWidth
                            height: desktops.cardHeight
                            radius: Theme.radiusLarge
                            color: "black"
                            border.width: desk.current || drop.containsDrag ? 2 : deskHover.hovered ? 1 : 0
                            border.color: desk.current || drop.containsDrag ? Theme.accent : Theme.stroke
                            Image {
                                anchors { fill: parent; margins: 3 }
                                source: "image://wallpaper/" + encodeURIComponent(Config.wallpaper)
                                sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                opacity: drop.containsDrag ? 0.6 : 1
                            }
                            // Le finestre del desktop, in piccolo: quante sono.
                            Row {
                                anchors { left: parent.left; bottom: parent.bottom; margins: 8 }
                                spacing: 4
                                Repeater {
                                    model: Math.min(6, root.windows.filter(w => w.workspace === desk.index).length)
                                    Rectangle { width: 14; height: 10; radius: 2; color: Qt.rgba(1, 1, 1, 0.75) }
                                }
                            }
                            // Chiudi il desktop, al passaggio del mouse.
                            Rectangle {
                                visible: deskHover.hovered && Desktops.count > 1
                                anchors { right: parent.right; top: parent.top; margins: 6 }
                                width: 24
                                height: 24
                                radius: Theme.radiusSmall
                                color: deskClose.containsMouse ? "#c42b1c" : Qt.rgba(0, 0, 0, 0.55)
                                z: 2
                                Text {
                                    anchors.centerIn: parent
                                    text: "✕"
                                    color: "white"
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    id: deskClose
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: Desktops.remove(desk.index)
                                }
                            }
                            HoverHandler {
                                id: deskHover
                                onHoveredChanged: {
                                    if (hovered) {
                                        peek.target = desk.index
                                        peek.restart()
                                    } else {
                                        peek.stop()
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: mouse => {
                                    if (mouse.button === Qt.RightButton) {
                                        root.openMenu(root.desktopMenu(desk.index), parent, mouse.x, mouse.y)
                                    } else {
                                        Desktops.switchTo(desk.index)
                                        root.close()
                                    }
                                }
                            }
                            DropArea {
                                id: drop
                                anchors.fill: parent
                                keys: ["vela-window"]
                                onDropped: drag => Desktops.moveWindow(drag.source.windowId, desk.index)
                            }
                        }
                        // Il nome; con "Rinomina" (o un doppio clic) si cambia.
                        Item {
                            width: desktops.cardWidth
                            height: 24
                            Text {
                                visible: root.renaming !== desk.index
                                anchors.centerIn: parent
                                width: parent.width
                                text: Desktops.names[desk.index] || ""
                                color: Theme.text
                                font.pixelSize: Theme.fontNormal
                                font.weight: desk.current ? Font.DemiBold : Font.Normal
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                MouseArea {
                                    anchors.fill: parent
                                    onDoubleClicked: root.renaming = desk.index
                                }
                            }
                            Rectangle {
                                visible: root.renaming === desk.index
                                anchors.fill: parent
                                radius: Theme.radiusSmall
                                color: Theme.dialog
                                border.width: 1
                                border.color: Theme.accent
                            }
                            MenuTextField {
                                visible: root.renaming === desk.index
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: TextInput.AlignVCenter
                                horizontalAlignment: TextInput.AlignHCenter
                                onVisibleChanged: {
                                    if (visible) {
                                        text = Desktops.names[desk.index]
                                        selectAll()
                                        forceActiveFocus()
                                    }
                                }
                                onAccepted: {
                                    if (text.trim() !== "") Desktops.rename(desk.index, text.trim())
                                    root.renaming = -1
                                    content.forceActiveFocus()
                                }
                                Keys.onEscapePressed: {
                                    root.renaming = -1
                                    content.forceActiveFocus()
                                }
                            }
                        }
                    }
                }

                // "Nuovo desktop" (anche come bersaglio: la finestra va su un desktop nuovo).
                Column {
                    spacing: 8
                    Rectangle {
                        width: desktops.cardWidth
                        height: desktops.cardHeight
                        radius: Theme.radiusLarge
                        color: newMouse.containsMouse || newDrop.containsDrag ? Theme.cardHover : Theme.card
                        border.width: newDrop.containsDrag ? 2 : 1
                        border.color: newDrop.containsDrag ? Theme.accent : Theme.stroke
                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: Theme.text
                            font.pixelSize: 36
                            font.weight: Font.Light
                        }
                        MouseArea {
                            id: newMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: Desktops.create(false)
                        }
                        DropArea {
                            id: newDrop
                            anchors.fill: parent
                            keys: ["vela-window"]
                            onDropped: drag => Desktops.moveWindowToNew(drag.source.windowId)
                        }
                    }
                    Text {
                        width: desktops.cardWidth
                        height: 24
                        text: "Nuovo desktop"
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    // Passando su un desktop, dopo un attimo se ne vedono le finestre.
    Timer {
        id: peek
        property int target: 0
        interval: 250
        onTriggered: root.shown = target
    }
}
