import QtQuick

// La finestra dei menu del tasto destro (docs/renderer.md §14): copre lo
// schermo ed è trasparente; i menu e i sottomenu sono pannelli al suo
// interno, e un clic fuori li chiude. Si apre con Menus.open().
Window {
    id: root
    objectName: "contextMenu"
    visible: false
    color: "transparent"

    property var panels: [] // un MenuPanel per livello, dal menu ai sottomenu
    property var options: ({})
    // Un sottomenu da aprire (o da chiudere) quando il mouse si ferma.
    property var pending: null

    Component {
        id: panelComponent
        MenuPanel {}
    }

    Connections {
        target: Menus
        function onOpenRequested(entries, x, y, options) {
            root.closeAll(false, true)
            root.options = options
            // Lo schermo chiesto, o il principale (lo schermo resta quello
            // dell'ultima volta finché non lo si cambia).
            {
                const name = options.screen || Shell.primaryScreen
                const screen = Qt.application.screens.find(s => s.name === name)
                if (screen && screen !== root.screen) {
                    root.visible = false // su un altro schermo: una superficie nuova
                    Shell.placeOnScreen(root, screen.name)
                }
            }
            Menus.isOpen = true
            root.visible = true
            root.openLevel(0, entries, {
                anchorX: x, anchorY: y,
                above: !!options.above, centered: !!options.centered,
                minWidth: options.minWidth || 0,
                keyboardMode: !!options.keyboard
            }, !!options.keyboard)
        }
        function onCloseRequested() {
            root.closeAll(true)
        }
    }

    // Il menu della finestra chiesto dal compositor: clic destro sulla
    // barra del titolo, Alt+Spazio, app che lo chiedono (show_window_menu).
    Connections {
        target: Shell
        function onWindowMenuRequested(window, output, x, y, maximized, resizable, keyboard) {
            Menus.open(Menus.windowEntries(maximized, false, resizable, action => Shell.windowAction(window, action)),
                x, y, { screen: output, keyboard: keyboard })
        }
    }

    function openLevel(level, entries, properties, selectFirst) {
        closeFrom(level)
        const panel = panelComponent.createObject(root.contentItem, Object.assign({ entries: entries, level: level }, properties))
        panel.activated.connect(entry => {
            root.closeAll(true)
            if (entry.action) {
                Qt.callLater(entry.action) // a menu chiuso: l'azione può aprire finestre
            }
        })
        panel.hovered.connect((index, rowY) => root.onHovered(panel, index, rowY))
        panel.submenuRequested.connect((index, rowY, keyboard) => root.openSubmenu(panel, index, rowY, keyboard))
        panel.contextRequested.connect((entry, px, py) => {
            root.openLevel(panel.level + 1, entry.context, {
                anchorX: px, anchorY: py, keyboardMode: panel.keyboardMode
            }, false)
        })
        panel.backRequested.connect(() => {
            if (panel.level > 0) {
                root.closeFrom(panel.level)
                root.panels[panel.level - 1].forceActiveFocus()
            } else {
                root.closeAll(true)
            }
        })
        panel.closeAllRequested.connect(() => root.closeAll(true))
        panel.pinToggled.connect(() => {
            if (root.options.rebuild && root.panels.length > 0) {
                root.closeFrom(1)
                root.panels[0].entries = root.options.rebuild()
            }
        })
        if (selectFirst) {
            panel.move(1)
        }
        panels.push(panel)
        panel.xChanged.connect(updateBlur)
        panel.yChanged.connect(updateBlur)
        panel.widthChanged.connect(updateBlur)
        panel.heightChanged.connect(updateBlur)
        updateBlur()
        panel.forceActiveFocus()
        return panel
    }

    // Lo sfondo sfocato sotto i menu aperti (la finestra copre lo schermo).
    function updateBlur() {
        Effects.setBlur(root, panels.map(p => Qt.rect(p.x, p.y, p.width, p.height)))
    }

    function openSubmenu(parentPanel, index, rowY, keyboard) {
        pending = null
        const child = panels[parentPanel.level + 1]
        if (child && child.parentIndex === index) {
            return // già aperto
        }
        const panel = openLevel(parentPanel.level + 1, parentPanel.entries[index].children, {
            anchorX: parentPanel.x + parentPanel.width - 4,
            altX: parentPanel.x + 4,
            anchorY: parentPanel.y + rowY - 4,
            keyboardMode: parentPanel.keyboardMode
        }, keyboard)
        panel.parentIndex = index
        parentPanel.currentIndex = index
    }

    // Come Windows: i sottomenu si aprono (e si chiudono) dopo una breve
    // pausa del mouse, così attraversare in diagonale altre voci per
    // raggiungerli non li chiude.
    function onHovered(panel, index, rowY) {
        const entry = panel.entries[index]
        const child = panels[panel.level + 1]
        if (child && child.parentIndex === index) {
            pending = null
            return
        }
        if (entry.children && entry.enabled !== false) {
            pending = { panel: panel, index: index, rowY: rowY }
        } else if (child) {
            pending = { panel: panel, index: -1 }
        } else {
            pending = null
        }
        // Entrare in un sottomenu annulla ciò che il padre aveva in sospeso.
        if (panel.level > 0) {
            const parent = panels[panel.level - 1]
            parent.currentIndex = panel.parentIndex
        }
        if (pending) {
            hoverTimer.restart()
        }
    }

    Timer {
        id: hoverTimer
        interval: 400
        onTriggered: {
            const p = root.pending
            root.pending = null
            if (!p || root.panels.indexOf(p.panel) < 0) {
                return
            }
            if (p.index >= 0) {
                root.openSubmenu(p.panel, p.index, p.rowY, false)
            } else {
                root.closeFrom(p.panel.level + 1)
                p.panel.forceActiveFocus()
            }
        }
    }

    function closeFrom(level) {
        while (panels.length > level) {
            panels.pop().destroy()
        }
        updateBlur()
    }

    function closeAll(notify, reopening) {
        const wasOpen = visible
        pending = null
        closeFrom(0)
        if (reopening) {
            return // un altro menu prende subito il posto di questo
        }
        visible = false
        Menus.isOpen = false
        if (wasOpen && notify && options.onClosed) {
            options.onClosed()
        }
    }

    onActiveChanged: {
        if (!active && visible) {
            closeAll(true)
        }
    }

    // Un clic fuori dai menu li chiude.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.closeAll(true)
    }
}
