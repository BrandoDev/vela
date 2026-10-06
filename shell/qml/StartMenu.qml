// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// Il menu Start: ricerca in alto, tutte le app sotto, utente in fondo.
// Si apre salendo e sfumando; si chiude al clic fuori o con Esc.
Window {
    id: root
    objectName: "startMenu"

    visible: false
    width: 660
    // Su schermi bassi si accorcia: la ricerca in alto deve restare visibile.
    height: Math.min(732, Screen.height - Theme.taskbarHeight - 12)
    color: "transparent"

    property bool closing: false
    // Chiesto una volta sola a logind: il computer sa sospendersi?
    readonly property bool canSuspend: Shell.canSuspend()

    // Le app aggiunte alla Start, installate, senza doppioni (stessa app
    // come pacchetto e come Flatpak).
    readonly property var pinned: {
        const seen = {}
        const out = []
        for (const id of Shell.startPins) {
            const e = Apps.entry(id)
            if (e.name && !seen[e.name]) {
                seen[e.name] = true
                out.push({ id: id, name: e.name, iconName: e.iconName })
            }
        }
        return out
    }
    readonly property bool showPinned: search.text.length === 0 && pinned.length > 0

    // Il menu Start sta centrato sopra la taskbar: da coordinate sue a
    // quelle dello schermo, per i menu del tasto destro.
    function screenPoint(item, x, y) {
        const p = item.mapToItem(null, x, y)
        return Qt.point((Screen.width - root.width) / 2 + p.x, Screen.height - Theme.taskbarHeight - root.height + p.y)
    }

    // Il menu di un'app (docs/renderer.md §14.7): in cima i file recenti e
    // le attività, come la jump list; poi le voci della Start.
    function appMenu(id, pinnedTile) {
        const entries = []
        const recent = Jumps.recent(id, 5)
        if (recent.length > 0) {
            entries.push({ header: "Recenti" })
            recent.forEach(f => entries.push({ text: f.name.replace(/&/g, "&&"), icon: f.icon, action: () => { Apps.launchWithFile(id, f.url); root.close() } }))
        }
        const actions = Apps.actions(id)
        if (actions.length > 0) {
            entries.push({ header: "Attività" })
            actions.forEach(a => entries.push({ text: a.name.replace(/&/g, "&&"), icon: a.icon, action: () => { Apps.launchAction(id, a.id); root.close() } }))
        }
        if (entries.length > 0) {
            entries.push({ separator: true })
        }
        const inStart = Shell.startPins.indexOf(id) >= 0
        const taskbar = Tasks.isPinned(id)
            ? { text: "Rimuovi dalla &barra delle applicazioni", icon: "window-unpin", action: () => Tasks.unpin(id) }
            : { text: "Aggiungi alla &barra delle applicazioni", icon: "window-pin", action: () => Tasks.pin(id) }
        const folder = { text: "Apri &percorso file", icon: "document-open-folder", action: () => { System.showInFolder(Apps.desktopFile(id)); root.close() } }
        const uninstall = { text: "&Disinstalla", icon: "edit-delete", enabled: System.canUninstall(Apps.desktopFile(id)), action: () => { System.uninstall(Apps.desktopFile(id)); root.close() } }
        if (pinnedTile) {
            entries.push(
                { text: "&Rimuovi da Start", icon: "window-unpin", action: () => Shell.unpinFromStart(id) },
                { text: "&Sposta all'inizio", icon: "go-top", enabled: Shell.startPins.indexOf(id) > 0, action: () => Shell.moveStartPinToFront(id) },
                taskbar, folder, uninstall)
        } else {
            entries.push(
                inStart
                    ? { text: "&Rimuovi da Start", icon: "window-unpin", action: () => Shell.unpinFromStart(id) }
                    : { text: "&Aggiungi a Start", icon: "window-pin", action: () => Shell.pinToStart(id) },
                { text: "A&ltro", children: [taskbar, folder] },
                uninstall)
        }
        return entries
    }

    function openAppMenu(item, x, y, id, pinnedTile) {
        const p = screenPoint(item, x, y)
        Menus.open(appMenu(id, pinnedTile), p.x, p.y)
    }

    // Una voce del menu di accensione.
    component PowerEntry: Item {
        id: entry
        property string icon // del tema; vuoto: il simbolo disegnato dentro
        property string label
        default property alias glyph: glyphSlot.data
        signal activated()
        width: parent ? parent.width : 0
        height: 36

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: entryMouse.pressed ? Theme.pressed : Theme.hover
            opacity: entryMouse.containsMouse ? 1 : 0
        }
        Image {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            visible: entry.icon !== ""
            source: entry.icon !== "" ? Theme.icons + entry.icon : ""
            sourceSize: Qt.size(width, height)
        }
        Item {
            id: glyphSlot
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
        }
        Text {
            x: 36
            anchors.verticalCenter: parent.verticalCenter
            text: entry.label
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
        MouseArea {
            id: entryMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: entry.activated()
        }
    }

    // Lo sfondo sfocato sotto il pannello (che sale aprendosi).
    function updateBlur() {
        Effects.setBlur(root, [Qt.rect(panel.x, Math.max(0, panel.y), panel.width, Math.max(0, Math.min(panel.height, root.height - panel.y)))])
    }
    Connections {
        target: panel
        function onYChanged() { root.updateBlur() }
        function onHeightChanged() { root.updateBlur() }
        function onWidthChanged() { root.updateBlur() }
    }

    function open() {
        closing = false
        powerButton.menuOpen = false
        Apps.query = ""
        search.text = ""
        grid.currentIndex = 0
        grid.positionViewAtBeginning()
        panel.opacity = 0
        panel.y = panel.restY + panel.slide
        Menus.placeOnTargetScreen(root) // lo schermo della taskbar da cui si apre
        visible = true
        Shell.startMenuOpen = true
        openAnimation.restart()
        search.forceActiveFocus()
    }

    function close() {
        if (!visible || closing) {
            return
        }
        closing = true
        Shell.startMenuOpen = false
        openAnimation.stop()
        closeAnimation.restart()
    }

    function launch(row) {
        if (row >= 0 && row < grid.count && Apps.launch(row)) {
            close()
        }
    }

    Connections {
        target: Shell
        function onToggleStartRequested() {
            if (root.visible && !root.closing) {
                root.close()
            } else {
                root.open()
            }
        }
    }

    // Clic su una finestra o altrove: il menu perde il focus e si chiude,
    // come su Windows.
    // Un menu del tasto destro aperto da qui prende la tastiera: il menu
    // Start resta aperto, e la riavrà quando il menu si chiude.
    // Chiuso il menu, la tastiera torna qui; se invece è andata altrove (una
    // finestra nuova, un clic), si chiude anche questo pannello.
    Connections {
        target: Menus
        function onIsOpenChanged() {
            if (!Menus.isOpen && root.visible) {
                focusCheck.restart()
            }
        }
    }
    Timer {
        id: focusCheck
        interval: 200
        onTriggered: {
            if (!root.active && root.visible && !Menus.isOpen) {
                root.close()
            }
        }
    }

    onActiveChanged: {
        if (!active && visible && !Menus.isOpen) {
            close()
        }
    }

    ParallelAnimation {
        id: openAnimation
        NumberAnimation {
            target: panel; property: "y"; to: panel.restY
            duration: Theme.slow
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
        }
        NumberAnimation {
            target: panel; property: "opacity"; to: 1
            duration: Theme.normal
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
        }
    }

    SequentialAnimation {
        id: closeAnimation
        ParallelAnimation {
            NumberAnimation {
                target: panel; property: "y"; to: panel.restY + panel.slide
                duration: Theme.normal
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.accelerate
            }
            NumberAnimation {
                target: panel; property: "opacity"; to: 0
                duration: Theme.normal
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.accelerate
            }
        }
        ScriptAction {
            script: {
                root.visible = false
                root.closing = false
            }
        }
    }

    // Ombra morbida attorno al pannello (non sotto: è acrylic).
    PanelShadow {
        target: panel
        opacity: panel.opacity
        blur: 40
        offset: Qt.vector2d(0, 8)
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    Rectangle {
        id: panel

        readonly property real restY: 10
        // Di quanto sale aprendosi. La parte che sporge sotto la finestra
        // viene tagliata al bordo della taskbar.
        readonly property real slide: 140
        x: 10
        y: restY
        width: root.width - 20
        height: root.height - 32 // 10 sopra, 22 sotto: 12 di stacco dalla taskbar
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke

        // --- ricerca ---
        Rectangle {
            id: searchBox
            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 24 }
            height: 38
            radius: height / 2
            color: Theme.surfaceRaised
            border.width: search.activeFocus ? 1 : 0
            border.color: Theme.accent

            Text {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                visible: search.text.length === 0
                text: qsTr("Cerca app")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }

            MenuTextField {
                id: search
                anchors { left: parent.left; right: parent.right; leftMargin: 18; rightMargin: 18; verticalCenter: parent.verticalCenter }
                mapToScreen: (x, y) => root.screenPoint(search, x, y)

                onTextChanged: {
                    Apps.query = text
                    grid.currentIndex = 0
                    grid.positionViewAtBeginning()
                }

                // La tastiera resta nella ricerca; le frecce muovono la griglia.
                Keys.onEscapePressed: root.close()
                Keys.onReturnPressed: root.launch(grid.currentIndex)
                Keys.onEnterPressed: root.launch(grid.currentIndex)
                Keys.onDownPressed: grid.moveCurrentIndexDown()
                Keys.onUpPressed: grid.moveCurrentIndexUp()
                Keys.onTabPressed: grid.moveCurrentIndexRight()
                Keys.onBacktabPressed: grid.moveCurrentIndexLeft()
            }
        }

        Text {
            id: sectionTitle
            anchors { top: searchBox.bottom; left: parent.left; topMargin: 20; leftMargin: 32 }
            text: search.text.length > 0 ? qsTr("Risultati") : root.showPinned ? qsTr("Aggiunte") : qsTr("Tutte le app")
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
        }

        // --- griglia delle app ---
        GridView {
            id: grid
            anchors {
                top: sectionTitle.bottom; bottom: footer.top
                left: parent.left; right: parent.right
                topMargin: 10; leftMargin: 20; rightMargin: 20; bottomMargin: 8
            }
            clip: true
            model: Apps
            cellWidth: Math.floor(width / 6)
            cellHeight: 96
            keyNavigationWraps: true
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: false

            // Con le frecce la selezione può uscire dalla vista: la seguiamo.
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)

            delegate: AppTile {
                id: tile
                required property string appId
                width: grid.cellWidth
                height: grid.cellHeight
                current: index === grid.currentIndex && search.text.length > 0
                onActivated: root.launch(index)
                onContextRequested: (x, y) => root.openAppMenu(tile, x, y, tile.appId, false)
            }

            // Sopra tutte le app, quelle aggiunte alla Start.
            header: Item {
                width: grid.width
                height: root.showPinned ? pinnedGrid.height + allTitle.height + 24 : 0
                visible: root.showPinned

                Grid {
                    id: pinnedGrid
                    columns: 6

                    Repeater {
                        model: root.pinned

                        delegate: AppTile {
                            id: pinnedTile
                            required property var modelData
                            width: grid.cellWidth
                            height: grid.cellHeight
                            name: modelData.name
                            iconName: modelData.iconName
                            comment: ""
                            onActivated: {
                                if (Apps.launchId(modelData.id)) {
                                    root.close()
                                }
                            }
                            onContextRequested: (x, y) => root.openAppMenu(pinnedTile, x, y, modelData.id, true)
                        }
                    }
                }
                Text {
                    id: allTitle
                    anchors { top: pinnedGrid.bottom; topMargin: 16; left: parent.left; leftMargin: 12 }
                    text: qsTr("Tutte le app")
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: qsTr("Nessuna app trovata")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
        }

        // --- piè di pagina: utente ---
        Rectangle {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 60
            color: "transparent"

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: Theme.stroke
            }

            Row {
                anchors { left: parent.left; leftMargin: 32; verticalCenter: parent.verticalCenter }
                spacing: 12

                Rectangle {
                    width: 32
                    height: 32
                    radius: 16
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: Shell.userInitial
                        color: "white"
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Shell.userName
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                }
            }

            // Accensione, dove Windows ha il suo pulsante: sospendi, esci
            // (chiude Vela, non il computer), riavvia, arresta.
            Item {
                id: powerButton
                anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
                width: 40
                height: 40
                property bool menuOpen: false

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: powerMouse.pressed ? Theme.pressed : Theme.hover
                    opacity: powerMouse.containsMouse || powerButton.menuOpen ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
                    }
                }

                // Il simbolo di accensione: un cerchio aperto in alto e una
                // barra.
                Shape {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    preferredRendererType: Shape.CurveRenderer
                    scale: powerMouse.pressed ? 0.9 : 1
                    Behavior on scale {
                        NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
                    }
                    ShapePath {
                        strokeColor: Theme.text
                        strokeWidth: 1.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathAngleArc { centerX: 10; centerY: 11; radiusX: 6.5; radiusY: 6.5; startAngle: -50; sweepAngle: 280 }
                    }
                    ShapePath {
                        strokeColor: Theme.text
                        strokeWidth: 1.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        startX: 10; startY: 3
                        PathLine { x: 10; y: 10 }
                    }
                }

                MouseArea {
                    id: powerMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: powerButton.menuOpen = !powerButton.menuOpen
                }

                Rectangle {
                    id: powerMenu
                    anchors { right: parent.right; bottom: parent.top; bottomMargin: 8 }
                    width: 200
                    height: powerEntries.implicitHeight + 8
                    radius: Theme.radiusLarge
                    color: Theme.popup
                    border.width: 1
                    border.color: Theme.stroke
                    opacity: powerButton.menuOpen ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
                    }

                    Column {
                        id: powerEntries
                        x: 4
                        y: 4
                        width: parent.width - 8

                        PowerEntry {
                            icon: "system-suspend"
                            label: "Sospendi"
                            visible: root.canSuspend
                            onActivated: { root.close(); Shell.suspend() }
                        }
                        PowerEntry {
                            label: "Esci"
                            onActivated: Shell.logout()

                            // Una porta aperta e una freccia che esce.
                            Shape {
                                anchors.fill: parent
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: Theme.text
                                    strokeWidth: 1.2
                                    fillColor: "transparent"
                                    capStyle: ShapePath.RoundCap
                                    joinStyle: ShapePath.RoundJoin
                                    startX: 8.5; startY: 2.5
                                    PathLine { x: 3; y: 2.5 }
                                    PathLine { x: 3; y: 13.5 }
                                    PathLine { x: 8.5; y: 13.5 }
                                }
                                ShapePath {
                                    strokeColor: Theme.text
                                    strokeWidth: 1.2
                                    fillColor: "transparent"
                                    capStyle: ShapePath.RoundCap
                                    joinStyle: ShapePath.RoundJoin
                                    startX: 6.5; startY: 8
                                    PathLine { x: 13.5; y: 8 }
                                    PathMove { x: 10.8; y: 5.3 }
                                    PathLine { x: 13.5; y: 8 }
                                    PathLine { x: 10.8; y: 10.7 }
                                }
                            }
                        }
                        PowerEntry {
                            icon: "system-reboot"
                            label: "Riavvia"
                            onActivated: Shell.reboot()
                        }
                        PowerEntry {
                            icon: "system-shutdown"
                            label: "Arresta"
                            onActivated: Shell.powerOff()
                        }
                    }
                }
            }
        }
    }
}
