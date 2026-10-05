import QtQuick

// Sistema > Schermo: la disposizione dei monitor (trascinabili), e per
// quello scelto scala, risoluzione, orientamento e frequenza. Ogni
// cambiamento chiede "Mantenere queste impostazioni?" e senza risposta
// torna indietro dopo 15 secondi, come Windows.
Page {
    id: page
    property int selected: 0
    readonly property var outputs: Displays.outputs
    readonly property var output: outputs.length > selected ? outputs[selected] : null
    readonly property var enabledOutputs: outputs.filter(o => o.enabled)

    Component.onCompleted: Displays.refresh()

    // --- cambiare e chiedere conferma ---
    function change(changes) {
        if (!output) {
            return
        }
        if (Displays.apply(output.name, changes)) {
            confirm.seconds = 15
            confirm.open()
            countdown.restart()
        }
    }
    Timer {
        id: countdown
        interval: 1000
        repeat: true
        onTriggered: {
            confirm.seconds -= 1
            if (confirm.seconds <= 0) {
                stop()
                confirm.close()
                Displays.revert()
            }
        }
    }

    // --- le modalità dello schermo scelto ---
    readonly property var resolutions: {
        if (!output) return []
        const seen = {}
        const list = []
        for (const m of output.modes) {
            const key = m.width + "x" + m.height
            if (!seen[key]) {
                seen[key] = true
                list.push({ width: m.width, height: m.height, preferred: m.preferred })
            } else if (m.preferred) {
                list.find(r => r.width === m.width && r.height === m.height).preferred = true
            }
        }
        list.sort((a, b) => b.width * b.height - a.width * a.height || b.width - a.width)
        return list
    }
    readonly property var refreshRates: {
        if (!output || !output.current) return []
        return output.modes.filter(m => m.width === output.current.width && m.height === output.current.height)
            .map(m => m.refresh).sort((a, b) => b - a)
            .filter((r, i, all) => i === 0 || Math.abs(all[i - 1] - r) > 0.01)
    }
    readonly property var scales: [1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 3]
    // La scala consigliata: quella che porta i punti a circa 96 per pollice
    // logico. Senza le dimensioni fisiche (wlr-randr non le dà), come
    // Windows: per altezza della risoluzione nativa.
    function recommendedScale(o) {
        const preferred = o.modes.find(m => m.preferred) || o.current
        if (!preferred) return 1
        return preferred.height >= 2160 ? 1.5 : preferred.height >= 1600 ? 1.25 : 1
    }
    readonly property var transforms: [
        { value: "normal", text: "Orizzontale" },
        { value: "90", text: "Verticale" },
        { value: "180", text: "Orizzontale (capovolto)" },
        { value: "270", text: "Verticale (capovolto)" }
    ]

    // --- la disposizione ---
    Rectangle {
        id: arrangement
        visible: page.outputs.length > 1
        width: parent.width
        height: 220
        radius: Theme.radiusCard
        color: Theme.card
        border.width: 1
        border.color: Theme.cardStroke

        // Tutto lo spazio degli schermi accesi, in scala nel riquadro.
        readonly property var bounds: {
            let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
            for (const o of page.enabledOutputs) {
                const w = o.current ? o.current.width / o.scale : 0
                const h = o.current ? o.current.height / o.scale : 0
                const rotated = o.transform === "90" || o.transform === "270"
                x0 = Math.min(x0, o.x); y0 = Math.min(y0, o.y)
                x1 = Math.max(x1, o.x + (rotated ? h : w)); y1 = Math.max(y1, o.y + (rotated ? w : h))
            }
            return { x: x0, y: y0, width: Math.max(1, x1 - x0), height: Math.max(1, y1 - y0) }
        }
        readonly property real zoom: Math.min((width - 80) / bounds.width, (height - 60) / bounds.height)

        Repeater {
            model: page.outputs
            delegate: Rectangle {
                id: monitor
                required property var modelData
                required property int index
                readonly property bool rotated: modelData.transform === "90" || modelData.transform === "270"
                readonly property real logicalWidth: modelData.current ? (rotated ? modelData.current.height : modelData.current.width) / modelData.scale : 0
                readonly property real logicalHeight: modelData.current ? (rotated ? modelData.current.width : modelData.current.height) / modelData.scale : 0
                readonly property real homeX: (arrangement.width - arrangement.bounds.width * arrangement.zoom) / 2 + (modelData.x - arrangement.bounds.x) * arrangement.zoom
                readonly property real homeY: (arrangement.height - arrangement.bounds.height * arrangement.zoom) / 2 + (modelData.y - arrangement.bounds.y) * arrangement.zoom
                visible: modelData.enabled
                x: homeX
                y: homeY
                width: logicalWidth * arrangement.zoom - 2
                height: logicalHeight * arrangement.zoom - 2
                radius: 4
                color: index === page.selected ? Qt.rgba(Theme.accentFill.r, Theme.accentFill.g, Theme.accentFill.b, 0.35) : Theme.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.12)
                border.width: index === page.selected ? 2 : 1
                border.color: index === page.selected ? Theme.accentFill : Theme.light ? Qt.rgba(0, 0, 0, 0.25) : Qt.rgba(1, 1, 1, 0.25)

                Text {
                    anchors.centerIn: parent
                    text: monitor.index + 1
                    color: Theme.text
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    anchors.fill: parent
                    drag.target: page.enabledOutputs.length > 1 ? monitor : null
                    drag.threshold: 4
                    onPressed: page.selected = monitor.index
                    onReleased: {
                        if (!drag.active && monitor.x === monitor.homeX && monitor.y === monitor.homeY) {
                            return
                        }
                        page.placeNextTo(monitor.index, monitor.x, monitor.y, monitor.width, monitor.height)
                        monitor.x = Qt.binding(() => monitor.homeX)
                        monitor.y = Qt.binding(() => monitor.homeY)
                    }
                }
            }
        }
    }

    // Lo schermo trascinato si attacca al lato più vicino di un altro, con
    // i bordi allineati se sono vicini: come Windows, niente buchi né
    // sovrapposizioni.
    function placeNextTo(index, px, py, pw, ph) {
        const moving = outputs[index]
        const zoom = arrangement.zoom
        const toLogicalX = v => (v - (arrangement.width - arrangement.bounds.width * zoom) / 2) / zoom + arrangement.bounds.x
        const toLogicalY = v => (v - (arrangement.height - arrangement.bounds.height * zoom) / 2) / zoom + arrangement.bounds.y
        const w = (pw + 2) / zoom
        const h = (ph + 2) / zoom
        const cx = toLogicalX(px) + w / 2
        const cy = toLogicalY(py) + h / 2
        let best = null
        for (let i = 0; i < outputs.length; ++i) {
            const o = outputs[i]
            if (i === index || !o.enabled || !o.current) continue
            const rotated = o.transform === "90" || o.transform === "270"
            const ow = (rotated ? o.current.height : o.current.width) / o.scale
            const oh = (rotated ? o.current.width : o.current.height) / o.scale
            const ocx = o.x + ow / 2
            const ocy = o.y + oh / 2
            const dx = cx - ocx
            const dy = cy - ocy
            let x, y
            if (Math.abs(dx) / (ow + w) > Math.abs(dy) / (oh + h)) {
                x = dx > 0 ? o.x + ow : o.x - w
                y = Math.max(o.y - h + 1, Math.min(o.y + oh - 1, cy - h / 2))
                if (Math.abs(y - o.y) < oh * 0.1) y = o.y // allineati in alto
            } else {
                y = dy > 0 ? o.y + oh : o.y - h
                x = Math.max(o.x - w + 1, Math.min(o.x + ow - 1, cx - w / 2))
                if (Math.abs(x - o.x) < ow * 0.1) x = o.x
            }
            const distance = Math.hypot(dx, dy)
            if (!best || distance < best.distance) best = { x: Math.round(x), y: Math.round(y), distance: distance }
        }
        if (best && (best.x !== moving.x || best.y !== moving.y)) {
            selected = index
            change({ x: best.x, y: best.y })
        }
    }

    Row {
        visible: page.outputs.length > 1
        width: parent.width
        height: 44
        spacing: 8
        layoutDirection: Qt.RightToLeft
        Choice {
            anchors.verticalCenter: parent.verticalCenter
            model: page.outputs.map((o, i) => ({ text: (i + 1) + ". " + (o.description || o.name) }))
            currentIndex: page.selected
            onChosen: index => page.selected = index
        }
    }

    Card {
        visible: !Displays.available
        icon: "dialog-warning"
        title: "Per cambiare gli schermi serve wlr-randr"
        description: "Installa il pacchetto wlr-randr e riapri questa pagina."
    }

    CardGroup {
        title: "Luminosità e colore"
        Card {
            visible: Status.brightnessAvailable
            icon: "brightness-high"
            title: "Luminosità"
            description: "Regola la luminosità dello schermo integrato"
            trailing: Slider {
                width: 200
                value: Status.brightness
                onMoved: value => Status.setBrightness(value)
            }
        }
        Card {
            icon: "redshift-status-on"
            title: "Luce notturna"
            description: "Usa colori più caldi per aiutarti a dormire"
            clickable: true
            chevron: true
            onClicked: root.navigate("night-light")
            trailing: Toggle {
                checked: Prefs.nightLight
                onToggled: on => Prefs.nightLight = on
            }
        }
    }

    CardGroup {
        visible: page.output !== null
        title: "Scala e layout"
        Card {
            icon: "zoom-in"
            title: "Scala"
            description: "Modifica le dimensioni di testo, app e altri elementi"
            trailing: Choice {
                model: page.scales.map(s => ({ text: Math.round(s * 100) + "%" + (page.output && Math.abs(s - page.recommendedScale(page.output)) < 0.01 ? " (consigliato)" : "") }))
                    .concat(page.output && page.scales.every(s => Math.abs(s - page.output.scale) > 0.001) ? [{ text: Math.round(page.output.scale * 100) + "% (personalizzata)" }] : [])
                currentIndex: {
                    if (!page.output) return -1
                    const i = page.scales.findIndex(s => Math.abs(s - page.output.scale) < 0.001)
                    return i >= 0 ? i : page.scales.length
                }
                onChosen: index => { if (index < page.scales.length) page.change({ scale: page.scales[index] }) }
            }
        }
        Card {
            icon: "view-fullscreen"
            title: "Risoluzione dello schermo"
            description: "Modifica la risoluzione per adattarla allo schermo collegato"
            trailing: Choice {
                model: page.resolutions.map(r => ({ text: r.width + " × " + r.height + (r.preferred ? " (consigliata)" : "") }))
                currentIndex: page.output && page.output.current
                    ? page.resolutions.findIndex(r => r.width === page.output.current.width && r.height === page.output.current.height) : -1
                onChosen: index => {
                    const r = page.resolutions[index]
                    // La frequenza più alta per quella risoluzione.
                    const modes = page.output.modes.filter(m => m.width === r.width && m.height === r.height)
                    modes.sort((a, b) => b.refresh - a.refresh)
                    page.change({ width: r.width, height: r.height, refresh: modes[0].refresh })
                }
            }
        }
        Card {
            icon: "object-rotate-right"
            title: "Orientamento dello schermo"
            trailing: Choice {
                model: page.transforms
                currentIndex: page.output ? Math.max(0, page.transforms.findIndex(t => t.value === page.output.transform)) : -1
                onChosen: index => page.change({ transform: page.transforms[index].value })
            }
        }
    }

    CardGroup {
        visible: page.output !== null && (page.refreshRates.some(r => r > 0) || page.outputs.length > 1)
        title: "Avanzate"
        Card {
            visible: page.refreshRates.some(r => r > 0)
            icon: "chronometer"
            title: "Frequenza di aggiornamento"
            description: "Una frequenza più alta rende il movimento più fluido, ma consuma più energia"
            trailing: Choice {
                model: page.refreshRates.map(r => ({ text: (Math.round(r * 100) / 100).toLocaleString(Qt.locale(), "f", r % 1 < 0.005 || r % 1 > 0.995 ? 0 : 2) + " Hz" }))
                currentIndex: page.output && page.output.current
                    ? page.refreshRates.findIndex(r => Math.abs(r - page.output.current.refresh) < 0.01) : -1
                onChosen: index => page.change({ width: page.output.current.width, height: page.output.current.height, refresh: page.refreshRates[index] })
            }
        }
        Card {
            visible: page.outputs.length > 1
            icon: "video-display"
            title: page.output && page.output.enabled ? "Disconnetti questo schermo" : "Usa questo schermo"
            description: page.output ? (page.output.description || page.output.name) + " (" + page.output.name + ")" : ""
            trailing: Button {
                text: page.output && page.output.enabled ? "Disconnetti" : "Attiva"
                usable: !(page.output && page.output.enabled && page.enabledOutputs.length <= 1)
                onClicked: page.change({ enabled: !page.output.enabled })
            }
        }
    }

    CardGroup {
        title: "Giochi"
        Card {
            icon: "chronometer"
            title: "Frequenza di aggiornamento variabile"
            description: "Il monitor si adegua ai fotogrammi del gioco (VRR, FreeSync, G-Sync): niente scatti né strappi. Con alcuni monitor il desktop può sfarfallare: per questo di solito solo a schermo intero"
            trailing: Choice {
                readonly property var modes: ["no", "giochi", "sempre"]
                model: ["Disattivata", "Giochi a schermo intero", "Sempre"]
                currentIndex: Math.max(0, modes.indexOf(Prefs.vrr))
                onChosen: index => Prefs.vrr = modes[index]
            }
        }
        Card {
            icon: "input-gaming"
            title: "Mostra subito ogni fotogramma nei giochi a schermo intero"
            description: "I giochi che lo chiedono vanno sullo schermo senza aspettare il monitor: meno latenza, con qualche strappo nell'immagine (tearing)"
            trailing: Toggle {
                checked: Prefs.tearing
                onToggled: on => Prefs.tearing = on
            }
        }
    }

    CardGroup {
        title: "Impostazioni correlate"
        Card {
            icon: "preferences-desktop-wallpaper"
            title: "Sfondo"
            clickable: true
            chevron: true
            onClicked: root.navigate("background")
        }
    }

    Dialog {
        id: confirm
        parent: root.contentItem
        property int seconds: 15
        title: "Mantenere queste impostazioni dello schermo?"
        primaryText: "Mantieni le modifiche"
        secondaryText: "Ripristina"
        onAccepted: countdown.stop()
        onRejected: {
            countdown.stop()
            Displays.revert()
        }
        Text {
            width: parent.width
            text: "Ripristino delle impostazioni precedenti dello schermo tra " + confirm.seconds + " secondi."
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }
}
