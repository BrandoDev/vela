// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// La taskbar: pulsante Start, app fissate e app aperte al centro, orologio
// a destra.
Window {
    id: root
    objectName: "taskbar"
    // Una per schermo (ScreenWindows): sul principale anche l'area di
    // notifica e le icone di sistema, sugli altri l'orologio, come Windows.
    property bool primary: false
    readonly property string screenName: screen ? screen.name : ""

    // Resta invisibile finché main.cpp non l'ha trasformata in un pannello
    // layer-shell. La larghezza la decide il compositor (ancorata ai lati).
    visible: false
    width: 1280
    height: Theme.taskbarHeight
    color: "transparent"

    // Le app fissate al primo avvio (id dei file .desktop; quelle non
    // installate si saltano). Poi valgono quelle scelte dall'utente: si
    // fissano e si tolgono dai menu, si riordinano trascinando i pulsanti.
    readonly property list<string> pinnedIds: [
        "vela-files.desktop",
        "org.kde.konsole.desktop",
        "firefox.desktop",
        "org.mozilla.firefox.desktop",
        "chromium.desktop",
        "org.kde.kate.desktop",
        "vela-settings.desktop"
    ]
    // Solo al primo avvio: poi valgono quelle salvate (aggiunte e tolte dai menu).
    Component.onCompleted: {
        if (!Tasks.pinsSaved) {
            Tasks.pinnedIds = pinnedIds
        }
        updateBlur()
    }

    // Tutta la taskbar sopra lo sfondo sfocato (acrylic).
    function updateBlur() {
        Effects.setBlur(root, [Qt.rect(0, 0, width, height)])
    }
    onWidthChanged: updateBlur()
    onHeightChanged: updateBlur()

    // Le anteprime delle finestre di un pulsante (TaskbarPreview.qml).
    Timer {
        id: previewDelay
        property var task: null
        interval: 450
        onTriggered: {
            if (!task || !task.hovered || task.windowCount === 0) return
            const p = task.mapToItem(null, task.width / 2, 0)
            Menus.preview = { key: task.key, appIds: Tasks.appIds(task.index), center: p.x, screen: root.screenName }
        }
    }

    // --- menu del tasto destro (docs/renderer.md §14.4-14.6) ---

    // La taskbar sta in fondo allo schermo: da coordinate sue a quelle dello schermo.
    function screenPoint(item, x, y) {
        const p = item.mapToItem(null, x, y)
        return Qt.point(p.x, Screen.height - root.height + p.y)
    }
    // I menu della taskbar salgono dal suo bordo superiore.
    readonly property real menuBottom: Screen.height - root.height - 4

    function openAbove(entries, item, centered, options) {
        const p = screenPoint(item, centered ? item.width / 2 : 0, 0)
        root.openMenu(entries, p.x, menuBottom, Object.assign({ above: true, centered: centered }, options || {}))
    }
    // I menu della taskbar si aprono sul suo schermo; anche i pannelli che
    // apre (Start, impostazioni rapide...) vanno lì.
    function openMenu(entries, x, y, options) {
        Menus.panelScreen = root.screenName
        Menus.open(entries, x, y, Object.assign({ screen: root.screenName }, options || {}))
    }
    function fromHere() { Menus.panelScreen = root.screenName }

    // La jump list di un pulsante: file fissati e recenti, attività
    // dell'app, poi l'app, fissa/togli, chiudi.
    function jumpList(task) {
        const id = task.desktopId
        const entries = []
        if (id !== "") {
            const fileEntry = (file, pinned) => ({
                text: file.name.replace(/&/g, "&&"),
                icon: file.icon,
                action: () => Apps.launchWithFile(id, file.url),
                pin: { pinned: pinned, toggle: () => Jumps.setPinned(id, file.url, !pinned) },
                context: [
                    { text: "&Apri", icon: task.iconName, action: () => Apps.launchWithFile(id, file.url) },
                    pinned
                        ? { text: "&Rimuovi da questo elenco", icon: "window-unpin", action: () => Jumps.setPinned(id, file.url, false) }
                        : { text: "A&ggiungi a questo elenco", icon: "window-pin", action: () => Jumps.setPinned(id, file.url, true) },
                    { text: "Rimuovi dall'&elenco", icon: "list-remove", action: () => Jumps.forget(id, file.url) }
                ]
            })
            const pinned = Jumps.pinned(id)
            if (pinned.length > 0) {
                entries.push({ header: "Aggiunti" })
                pinned.forEach(f => entries.push(fileEntry(f, true)))
            }
            const recent = Jumps.recent(id, 10)
            if (recent.length > 0) {
                entries.push({ header: "Recenti" })
                recent.forEach(f => entries.push(fileEntry(f, false)))
            }
            const actions = Apps.actions(id)
            if (actions.length > 0) {
                entries.push({ header: "Attività" })
                actions.forEach(a => entries.push({ text: a.name.replace(/&/g, "&&"), icon: a.icon, action: () => Apps.launchAction(id, a.id) }))
            }
            if (entries.length > 0) {
                entries.push({ separator: true })
            }
            entries.push({ text: task.name.replace(/&/g, "&&"), icon: task.iconName, action: () => Apps.launchId(id) })
            entries.push(Tasks.isPinned(id)
                ? { text: "Rimuovi dalla barra delle applicazioni", icon: "window-unpin", action: () => Tasks.unpin(id) }
                : { text: "Aggiungi alla barra delle applicazioni", icon: "window-pin", action: () => Tasks.pin(id) })
        }
        if (task.windowCount > 0 && Config.endTask) {
            entries.push({ text: "Termina attività", icon: "process-stop", action: () => Tasks.endTask(task.index) })
        }
        if (task.windowCount > 0) {
            entries.push({
                text: task.windowCount > 1 ? "Chiudi tutte le finestre" : "Chiudi finestra",
                icon: "window-close",
                action: () => Tasks.closeWindows(task.index)
            })
        }
        return entries
    }

    // Win+X: il menu del pulsante Start.
    function winXEntries() {
        const item = (text, icon, name) => ({ text: text, icon: icon, enabled: System.available(name), action: () => System.trigger(name) })
        const entries = [item("App insta&llate", "system-software-install", "installed-apps")]
        if (System.isLaptop()) {
            entries.push(item("Centro PC &portatile", "computer-laptop", "mobility"))
        }
        entries.push(
            item("&Opzioni risparmio energia", "preferences-system-power-management", "power"),
            item("Visuali&zzatore eventi", "text-x-log", "events"),
            item("Siste&ma", "computer", "system"),
            item("Gestione dispositi&vi", "preferences-devices-tree", "devices"),
            item("Conn&essioni di rete", "preferences-system-network", "network"),
            item("Gestio&ne disco", "drive-harddisk", "disks"),
            item("&Gestione computer", "computer", "computer"),
            { separator: true },
            item("Te&rminale", "utilities-terminal", "terminal"),
            item("Terminale (A&dmin)", "utilities-terminal", "terminal-admin"),
            { separator: true },
            item("Gestione attivi&tà", "utilities-system-monitor", "task-manager"),
            item("&Impostazioni", "preferences-system", "settings"),
            item("&Esplora file", "system-file-manager", "files"),
            { text: "&Cerca", icon: "search", action: () => { root.fromHere(); if (!Shell.startMenuOpen) Shell.toggleStartMenu() } },
            { text: "E&segui", icon: "system-run", action: () => { root.fromHere(); Shell.runRequested() } },
            { separator: true },
            { text: "&Arresta il sistema o disconnetti", icon: "system-shutdown", children: [
                { text: "&Disconnetti", icon: "system-log-out", action: () => Shell.logout() },
                { text: "&Sospendi", icon: "system-suspend", enabled: Shell.canSuspend(), action: () => Shell.suspend() },
                { text: "&Arresta il sistema", icon: "system-shutdown", action: () => Shell.powerOff() },
                { text: "&Riavvia il sistema", icon: "system-reboot", action: () => Shell.reboot() }
            ] },
            { text: "Des&ktop", icon: "user-desktop", action: () => Tasks.toggleDesktop() }
        )
        return entries
    }

    Connections {
        target: Shell
        enabled: root.primary
        function onWinXRequested() {
            root.openWinX(true)
        }
        function onShowDesktopRequested() {
            Tasks.toggleDesktop()
        }
    }
    function openWinX(keyboard) {
        const p = screenPoint(startButton, 0, 0)
        root.openMenu(winXEntries(), p.x, menuBottom, { above: true, keyboard: keyboard })
    }

    // Il menu di un'icona dell'area di notifica (lo decide l'app).
    Connections {
        target: Tray
        enabled: root.primary
        function onMenuReady(row, anchorX, entries) {
            const convert = list => list.map(e => e.separator ? { separator: true } : {
                text: e.label,
                icon: e.icon,
                enabled: e.enabled,
                checked: e.checkable ? e.checked : undefined,
                radio: e.radio,
                children: e.children.length > 0 ? convert(e.children) : undefined,
                action: () => Tray.activateMenuEntry(row, e.id)
            })
            root.openMenu(convert(entries), anchorX, root.menuBottom, { above: true, centered: true })
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.taskbar

        // Spazio vuoto: Gestione attività e impostazioni della taskbar.
        MouseArea {
            id: emptyArea
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: mouse => {
                const p = root.screenPoint(emptyArea, mouse.x, 0)
                root.openMenu([
                    { text: "Gestione &attività", icon: "utilities-system-monitor", enabled: System.available("task-manager"), action: () => System.trigger("task-manager") },
                    { text: "&Impostazioni della barra delle applicazioni", icon: "configure", enabled: System.available("taskbar-settings"), action: () => System.trigger("taskbar-settings") }
                ], p.x, root.menuBottom, { above: true })
            }
        }

        // Sottile riga di luce sul bordo superiore, come un vetro.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: Theme.stroke
        }
    }

    Row {
        id: buttons
        // Centrata a mano invece che con anchors: così, quando un'app si
        // apre o si chiude, la fila scivola al nuovo centro invece di saltare.
        // Oppure a sinistra, come le versioni precedenti di Windows.
        x: Config.taskbarAlignment === "left" ? 12 : Math.round((parent.width - width) / 2)
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Behavior on x {
            NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        }
        // Il pulsante di un'app appena aperta entra crescendo.
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal }
            NumberAnimation {
                property: "scale"; from: 0.5; to: 1; duration: Theme.slow
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
            }
        }
        move: Transition {
            NumberAnimation {
                property: "x"; duration: Theme.normal
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
            }
        }

        TaskbarButton {
            id: startButton
            active: Shell.startMenuOpen
            onClicked: { root.fromHere(); Shell.toggleStartMenu() }
            onRightClicked: root.openWinX(false)
            StartGlyph { anchors.centerIn: parent }
        }

        // Visualizzazione attività: due finestre sovrapposte, come l'icona di Windows 11.
        TaskbarButton {
            visible: Config.taskView
            tooltip: "Visualizzazione attività"
            onClicked: { root.fromHere(); Shell.taskViewRequested() }
            Item {
                anchors.centerIn: parent
                width: 20
                height: 18
                Rectangle {
                    x: 0
                    y: 4
                    width: 13
                    height: 13
                    radius: 2.5
                    color: "transparent"
                    border.width: 1.5
                    border.color: Theme.text
                }
                Rectangle {
                    x: 6
                    y: 0
                    width: 13
                    height: 13
                    radius: 2.5
                    color: Theme.accentLight
                    opacity: 0.9
                }
            }
        }

        Repeater {
            id: taskRepeater
            model: Tasks

            TaskbarButton {
                id: task
                draggable: true
                // Lasciato più in là: si sposta di tanti posti quanti pulsanti ha superato.
                onDropped: dx => {
                    const to = Math.max(0, Math.min(taskRepeater.count - 1, index + Math.round(dx / (width + buttons.spacing))))
                    if (to !== index) Tasks.move(index, to)
                }
                onDraggingChanged: if (dragging) Menus.preview = null
                required property int index
                required property string key
                required property string name
                required property string iconName
                required property int windowCount
                required property bool windowActive
                required property string desktopId

                tooltip: name
                running: windowCount > 0
                active: windowActive
                onClicked: {
                    Menus.preview = null
                    Tasks.activate(index)
                }
                onMiddleClicked: Tasks.launchNew(index)
                // Le anteprime delle sue finestre, col mouse fermo sul pulsante
                // (subito, se quelle di un altro pulsante sono già aperte).
                onHoveredChanged: {
                    Menus.previewButtonHovered = hovered
                    if (hovered && windowCount > 0) {
                        previewDelay.task = task
                        if (Menus.preview) {
                            previewDelay.triggered()
                        } else {
                            previewDelay.restart()
                        }
                    } else if (previewDelay.task === task) {
                        previewDelay.stop()
                    }
                }
                onRightClicked: shift => {
                    Menus.preview = null
                    if ((shift || Shell.shiftHeld()) && windowCount > 0) {
                        const state = Tasks.windowState(index)
                        root.openAbove(Menus.windowEntries(state.maximized, state.minimized, true,
                            action => Tasks.windowAction(task.index, action)), task, true)
                    } else {
                        root.openAbove(root.jumpList(task), task, true, { minWidth: 256, rebuild: () => root.jumpList(task) })
                    }
                }

                // Il compositor fa volare qui le finestre ridotte a icona.
                function reportGeometry() {
                    if (windowCount > 0) {
                        const p = mapToItem(null, 0, 0)
                        Tasks.setButtonGeometry(index, root, Qt.rect(p.x, p.y, width, height))
                    }
                }
                onXChanged: reportGeometry()
                onWindowCountChanged: reportGeometry()
                Connections {
                    target: buttons
                    function onXChanged() { task.reportGeometry() }
                }

                Image {
                    anchors.fill: parent
                    source: Theme.icons + encodeURIComponent(task.iconName)
                    sourceSize: Qt.size(width, height)
                    smooth: true
                    mipmap: true
                }
            }
        }
    }

    // Area di notifica: le icone delle app (Telegram, Discord, Steam...),
    // a sinistra dell'orologio come su Windows.
    Row {
        id: tray
        visible: root.primary
        anchors { right: systemIcons.left; top: parent.top; bottom: parent.bottom; rightMargin: 4 }

        Repeater {
            model: Tray

            delegate: Item {
                id: trayItem
                required property int index
                required property string icon
                required property string title
                required property bool attention
                width: icon !== "" ? 32 : 0
                height: tray.height
                visible: icon !== ""

                Rectangle {
                    anchors { fill: parent; topMargin: 8; bottomMargin: 8 }
                    radius: Theme.radiusSmall
                    color: trayMouse.pressed ? Theme.pressed : Theme.hover
                    opacity: trayMouse.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                }
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    source: trayItem.icon
                    sourceSize: Qt.size(width, height)
                    smooth: true
                    mipmap: true
                }
                // Chiede attenzione (messaggi non letti): un puntino.
                Rectangle {
                    visible: trayItem.attention
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.accent
                    anchors { right: parent.right; top: parent.top; rightMargin: 6; topMargin: 12 }
                }

                MouseArea {
                    id: trayMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onClicked: mouse => {
                        const center = trayItem.mapToItem(null, trayItem.width / 2, 0).x
                        if (mouse.button === Qt.RightButton) {
                            Tray.requestMenu(trayItem.index, center)
                        } else if (mouse.button === Qt.MiddleButton) {
                            Tray.secondaryActivate(trayItem.index)
                        } else {
                            Tray.activate(trayItem.index, center)
                        }
                    }
                    onWheel: wheel => Tray.scroll(trayItem.index, wheel.angleDelta.y)
                }
            }
        }
    }

    // Le icone di sistema (rete, volume, batteria) in un solo pulsante: apre
    // le impostazioni rapide, come su Windows 11. La rotellina sul volume lo
    // cambia; col tasto destro i loro menu (§14.5).
    Item {
        id: systemIcons
        visible: root.primary
        anchors { right: clock.left; top: parent.top; bottom: parent.bottom; rightMargin: 2 }
        width: visible ? iconsRow.width + 16 : 0

        Rectangle {
            anchors { fill: parent; topMargin: 4; bottomMargin: 4 }
            radius: Theme.radiusSmall
            color: systemMouse.pressed ? Theme.pressed : Theme.hover
            opacity: systemMouse.containsMouse || Menus.quickSettingsOpen ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        Row {
            id: iconsRow
            anchors.centerIn: parent
            spacing: 10
            Image {
                id: networkIcon
                width: 16
                height: 16
                source: Theme.icons + encodeURIComponent(Status.networkIconName)
                sourceSize: Qt.size(width, height)
            }
            Image {
                id: volumeIcon
                visible: Status.volumeAvailable
                width: 16
                height: 16
                source: Theme.icons + encodeURIComponent(Status.volumeIconName)
                sourceSize: Qt.size(width, height)
            }
            Image {
                visible: Status.batteryPresent
                width: 16
                height: 16
                source: Theme.icons + encodeURIComponent(Status.batteryCharging ? "battery-good-charging" : "battery-good")
                sourceSize: Qt.size(width, height)
            }
        }
        MouseArea {
            id: systemMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.LeftButton) {
                    root.fromHere()
                    Shell.quickSettingsRequested()
                    return
                }
                // Il menu dell'icona sotto il mouse: volume o rete.
                const p = root.screenPoint(systemMouse, mouse.x, 0)
                const overVolume = volumeIcon.visible && mouse.x >= volumeIcon.x + iconsRow.x - 5
                    && mouse.x < volumeIcon.x + iconsRow.x + volumeIcon.width + 5
                root.openMenu(overVolume ? [
                    { text: "Apri &mixer volume", icon: "audio-volume-high", enabled: System.available("volume-mixer"), action: () => System.trigger("volume-mixer") },
                    { text: "&Impostazioni audio", icon: "preferences-desktop-sound", enabled: System.available("sound-settings"), action: () => System.trigger("sound-settings") }
                ] : [
                    { text: "&Diagnostica problemi di rete", icon: "network-workgroup", enabled: false },
                    { text: "&Impostazioni di rete e Internet", icon: "preferences-system-network", enabled: System.available("network"), action: () => System.trigger("network") }
                ], p.x, root.menuBottom, { above: true })
            }
            onWheel: wheel => {
                if (Status.volumeAvailable) {
                    Status.setVolume(Status.volume + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
                }
            }
        }
    }

    // Orologio: ora sopra, data sotto.
    Item {
        id: clock
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom; rightMargin: 12 }
        width: clockColumn.implicitWidth + 16 + (Notifications.history.count > 0 || Notifications.doNotDisturb ? 22 : 0)

        property date now: new Date()

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: clock.now = new Date()
        }

        Rectangle {
            anchors { fill: parent; topMargin: 4; bottomMargin: 4 }
            radius: Theme.radiusSmall
            color: Theme.hover
            opacity: clockMouse.containsMouse || Menus.notificationCenterOpen ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        // Le notifiche da leggere: un numero accanto all'orologio, come Windows
        // 11 (con "Non disturbare" una campana spenta).
        Rectangle {
            visible: Notifications.history.count > 0 || Notifications.doNotDisturb
            anchors { right: parent.right; rightMargin: -2; verticalCenter: parent.verticalCenter }
            width: 18
            height: 18
            radius: 9
            color: Notifications.doNotDisturb ? "transparent" : Theme.accent
            Text {
                anchors.centerIn: parent
                visible: !Notifications.doNotDisturb
                text: Math.min(Notifications.history.count, 9)
                color: "white"
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            Image {
                anchors.centerIn: parent
                visible: Notifications.doNotDisturb
                width: 14
                height: 14
                source: Theme.icons + "notifications-disabled"
                sourceSize: Qt.size(width, height)
            }
        }

        Column {
            id: clockColumn
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 1

            Text {
                anchors.right: parent.right
                text: Qt.formatTime(clock.now, "HH:mm")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
            Text {
                anchors.right: parent.right
                text: Qt.formatDate(clock.now, "dd/MM/yyyy")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
        }

        MouseArea {
            id: clockMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.LeftButton) {
                    root.fromHere()
                    Shell.notificationCenterRequested()
                    return
                }
                const p = root.screenPoint(clock, mouse.x, 0)
                root.openMenu([
                    { text: "&Regola data e ora", icon: "preferences-system-time", enabled: System.available("datetime"), action: () => System.trigger("datetime") },
                    { text: "Impostazioni di &notifica", icon: "preferences-desktop-notification", enabled: System.available("notification-settings"), action: () => System.trigger("notification-settings") }
                ], p.x, root.menuBottom, { above: true })
            }
        }
    }
}
