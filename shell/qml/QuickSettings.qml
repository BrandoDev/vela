import QtQuick
import QtQuick.Shapes

// Le impostazioni rapide (Win+A, o clic sulle icone di sistema della
// taskbar), come Windows 11: riquadri da accendere e spegnere (Wi-Fi,
// Bluetooth, modalità aereo, risparmio energia, Luce notturna...),
// luminosità e volume, batteria e Impostazioni in fondo. Pannello acrylic in
// basso a destra. Alcuni riquadri hanno una pagina loro: la freccia del
// Wi-Fi elenca le reti per sceglierne una, Accessibilità accende lente,
// filtri colore e tasti permanenti.
Window {
    id: root
    objectName: "quickSettings"
    visible: false
    width: 384
    height: panel.height + 24
    color: "transparent"

    // "" le impostazioni rapide; "wifi" e "accessibility" le loro pagine.
    property string page: ""

    function open() {
        Menus.notificationCenterOpen = false
        page = ""
        Menus.placeOnTargetScreen(root)
        visible = true
        Menus.quickSettingsOpen = true
        panel.opacity = 0
        slide.y = 12
        appear.restart()
        panel.forceActiveFocus()
        updateBlur()
    }
    function close() {
        visible = false
        Menus.quickSettingsOpen = false
    }
    function toggle() {
        if (visible) {
            close()
        } else {
            open()
        }
    }
    function showPage(name) {
        page = name
        pageIn.restart()
        if (name === "wifi") {
            wifiPage.expanded = ""
            wifiPage.password = ""
            Network.refreshWifi()
            Network.scan()
        }
        panel.forceActiveFocus()
    }
    // In basso a destra, sopra la taskbar: dal pannello allo schermo.
    function screenPoint(item, x, y) {
        const p = item.mapToItem(null, x, y)
        return Qt.point(Screen.width - root.width + p.x, Screen.height - Theme.taskbarHeight - root.height + p.y)
    }
    function openSettings(name) {
        close()
        System.trigger(name)
    }

    Connections {
        target: Shell
        function onQuickSettingsRequested() { root.toggle() }
        function onNotificationCenterRequested() { root.close() }
    }

    onActiveChanged: {
        if (!active && visible && !Menus.isOpen) {
            close()
        }
    }

    function updateBlur() {
        Effects.setBlur(root, [Qt.rect(panel.x, panel.y, panel.width, panel.height)])
    }

    ParallelAnimation {
        id: appear
        NumberAnimation { target: panel; property: "opacity"; to: 1; duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        NumberAnimation { target: slide; property: "y"; to: 0; duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
    }
    // Una pagina entra da destra (indietro: come le altre, è breve).
    ParallelAnimation {
        id: pageIn
        NumberAnimation { target: pages; property: "opacity"; from: 0; to: 1; duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        NumberAnimation { target: pageSlide; property: "x"; from: 24; to: 0; duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
    }

    // Un riquadro: acceso (colore d'accento) o spento, con l'etichetta
    // sotto. `split`: a destra una freccia che apre la sua pagina (Wi-Fi);
    // `menu`: tutto il riquadro apre la pagina (Accessibilità).
    component Tile: Item {
        id: tile
        property string icon
        property string label
        property bool checked
        property bool usable: true
        property bool split: false
        property bool menu: false
        signal toggled()
        signal details()
        signal contextMenu()
        width: 104
        height: 82
        opacity: usable ? 1 : 0.45

        Rectangle {
            id: button
            width: parent.width
            height: 48
            radius: Theme.radiusSmall
            color: tile.checked ? Theme.accent : Theme.surfaceRaised
            border.width: tile.checked ? 0 : 1
            border.color: Theme.stroke
            clip: true

            // Le due metà (o il riquadro intero), ognuna col suo hover.
            Rectangle {
                x: 0
                width: tile.split ? parent.width - 36 : parent.width
                height: parent.height
                radius: parent.radius
                color: tile.checked ? "white" : Theme.overlay
                opacity: mainMouse.pressed ? 0.06 : mainMouse.containsMouse && tile.usable ? 0.08 : 0
            }
            Rectangle {
                visible: tile.split
                x: parent.width - 36
                width: 36
                height: parent.height
                radius: parent.radius
                color: tile.checked ? "white" : Theme.overlay
                opacity: detailsMouse.pressed ? 0.06 : detailsMouse.containsMouse && tile.usable ? 0.08 : 0
            }
            Rectangle {
                visible: tile.split
                x: parent.width - 36
                y: 10
                width: 1
                height: parent.height - 20
                color: tile.checked ? Qt.rgba(1, 1, 1, 0.35) : Theme.stroke
            }
            Image {
                x: (tile.split ? parent.width - 36 : parent.width) / 2 - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                source: (tile.checked ? Theme.iconsOnAccent : Theme.icons) + encodeURIComponent(tile.icon)
                sourceSize: Qt.size(width, height)
            }
            // La freccia ">".
            Shape {
                visible: tile.split || tile.menu
                x: tile.split ? parent.width - 21 : parent.width - 18
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 10
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: tile.checked ? "white" : Theme.text
                    strokeWidth: 1.2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: 1; startY: 1
                    PathLine { x: 5; y: 5 }
                    PathLine { x: 1; y: 9 }
                }
            }
            MouseArea {
                id: mainMouse
                width: tile.split ? parent.width - 36 : parent.width
                height: parent.height
                hoverEnabled: true
                enabled: tile.usable
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        tile.contextMenu()
                    } else if (tile.menu) {
                        tile.details()
                    } else {
                        tile.toggled()
                    }
                }
            }
            MouseArea {
                id: detailsMouse
                visible: tile.split
                x: parent.width - 36
                width: 36
                height: parent.height
                hoverEnabled: true
                enabled: tile.usable
                onClicked: tile.details()
            }
        }
        Text {
            anchors { top: button.bottom; topMargin: 6; horizontalCenter: parent.horizontalCenter }
            width: parent.width
            text: tile.label
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    // Un cursore (luminosità, volume) con la sua icona a sinistra.
    component Slider: Item {
        id: slider
        property string icon
        property real value
        signal moved(real value)
        signal iconClicked()
        width: parent.width
        height: 36

        Item {
            id: iconButton
            width: 36
            height: 36
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: Theme.hover
                opacity: iconMouse.containsMouse ? 1 : 0
            }
            Image {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: Theme.icons + encodeURIComponent(slider.icon)
                sourceSize: Qt.size(width, height)
            }
            MouseArea {
                id: iconMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: slider.iconClicked()
            }
        }
        Item {
            id: track
            anchors { left: iconButton.right; leftMargin: 12; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            height: 20
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4
                radius: 2
                color: Theme.stroke
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * slider.value
                height: 4
                radius: 2
                color: Theme.accent
            }
            Rectangle {
                x: parent.width * slider.value - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: Theme.popup
                border.width: 1
                border.color: Theme.stroke
                Rectangle {
                    anchors.centerIn: parent
                    width: trackMouse.pressed ? 8 : 10
                    height: width
                    radius: width / 2
                    color: Theme.accent
                }
            }
            MouseArea {
                id: trackMouse
                anchors { fill: parent; margins: -8 }
                function update(x) { slider.moved(Math.max(0, Math.min(1, (x - 8) / track.width))) }
                onPressed: mouse => update(mouse.x)
                onPositionChanged: mouse => { if (pressed) update(mouse.x) }
                onWheel: wheel => slider.moved(Math.max(0, Math.min(1, slider.value + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))))
            }
        }
    }

    // Un interruttore come quelli di Windows 11.
    component Switch: Item {
        id: toggle
        property bool checked
        signal toggled()
        width: 40
        height: 20
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: toggle.checked ? Theme.accent : "transparent"
            border.width: toggle.checked ? 0 : 1
            border.color: Theme.textDim
            Rectangle {
                x: toggle.checked ? parent.width - width - 4 : 4
                anchors.verticalCenter: parent.verticalCenter
                width: switchMouse.containsMouse ? 14 : 12
                height: width
                radius: width / 2
                color: toggle.checked ? "white" : Theme.textDim
                Behavior on x { NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate } }
            }
        }
        MouseArea {
            id: switchMouse
            anchors { fill: parent; margins: -4 }
            hoverEnabled: true
            onClicked: toggle.toggled()
        }
    }

    // L'intestazione di una pagina: indietro, il titolo, e a destra ciò che serve.
    component PageHeader: Item {
        id: header
        property string title
        default property alias trailing: trailingSlot.data
        width: parent.width
        height: 36
        Item {
            id: back
            width: 32
            height: 32
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: Theme.hover
                opacity: backMouse.containsMouse ? 1 : 0
            }
            Image {
                anchors.centerIn: parent
                width: 16
                height: 16
                source: Theme.icons + "go-previous"
                sourceSize: Qt.size(width, height)
            }
            MouseArea {
                id: backMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.showPage("")
            }
        }
        Text {
            anchors { left: back.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
            text: header.title
            color: Theme.text
            font.pixelSize: Theme.fontNormal + 2
            font.weight: Font.DemiBold
        }
        Item {
            id: trailingSlot
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: childrenRect.width
            height: childrenRect.height
        }
    }

    // Un collegamento in fondo a una pagina ("Altre impostazioni...").
    component Link: Text {
        id: link
        property string target
        color: linkMouse.containsMouse ? Theme.accentLight : Theme.accent
        font.pixelSize: Theme.fontNormal
        font.underline: linkMouse.containsMouse
        MouseArea {
            id: linkMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openSettings(link.target)
        }
    }

    Rectangle {
        id: panel
        x: 12
        y: 12
        width: root.width - 24
        height: root.page === "" ? content.height + 24 + footer.height
            : root.page === "wifi" ? wifiPage.height + 32 : accessPage.height + 32
        radius: Theme.radiusMenu
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
        transform: Translate { id: slide }
        focus: true
        clip: true
        Keys.onEscapePressed: {
            if (root.page !== "") {
                root.showPage("")
            } else {
                root.close()
            }
        }
        onHeightChanged: root.updateBlur()

        Item {
            id: pages
            anchors.fill: parent
            transform: Translate { id: pageSlide }

            // ---------------------------------------------------- principale --
            Column {
                id: content
                visible: root.page === ""
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 16

                Grid {
                    columns: 3
                    columnSpacing: 8
                    rowSpacing: 4

                    Tile {
                        visible: Status.wifiAvailable
                        icon: Status.wifiEnabled ? "network-wireless" : "network-wireless-disconnected"
                        label: Status.networkWireless && Status.networkName !== "" ? Status.networkName : "Wi-Fi"
                        checked: Status.wifiEnabled
                        split: Network.available
                        onToggled: Status.setWifiEnabled(!Status.wifiEnabled)
                        onDetails: root.showPage("wifi")
                        onContextMenu: root.openSettings("network")
                    }
                    // Senza Wi-Fi (un fisso col cavo): la rete com'è, a titolo informativo.
                    Tile {
                        visible: !Status.wifiAvailable
                        icon: Status.networkIconName
                        label: Status.networkConnected ? (Status.networkName !== "" ? Status.networkName : "Rete") : "Non connesso"
                        checked: Status.networkConnected
                        onToggled: root.openSettings("network")
                        onContextMenu: root.openSettings("network")
                    }
                    Tile {
                        visible: Status.bluetoothAvailable
                        icon: Status.bluetoothEnabled ? "network-bluetooth" : "network-bluetooth-inactive"
                        label: "Bluetooth"
                        checked: Status.bluetoothEnabled
                        split: true
                        onToggled: Status.setBluetoothEnabled(!Status.bluetoothEnabled)
                        onDetails: root.openSettings("bluetooth-settings")
                        onContextMenu: root.openSettings("bluetooth-settings")
                    }
                    Tile {
                        visible: Status.wifiAvailable || Status.bluetoothAvailable
                        icon: "network-flightmode-on"
                        label: "Modalità aereo"
                        checked: Status.airplane
                        onToggled: Status.setAirplaneMode(!Status.airplane)
                    }
                    Tile {
                        visible: Status.powerSaverAvailable
                        icon: "battery-profile-powersave"
                        label: "Risparmio energia"
                        checked: Status.powerSaver
                        onToggled: Status.setPowerSaver(!Status.powerSaver)
                        onContextMenu: root.openSettings("power")
                    }
                    Tile {
                        icon: "redshift-status-on"
                        label: "Luce notturna"
                        checked: Access.nightLight
                        onToggled: Access.nightLight = !Access.nightLight
                        onContextMenu: root.openSettings("night-light-settings")
                    }
                    Tile {
                        icon: "preferences-desktop-accessibility"
                        label: "Accessibilità"
                        checked: Access.magnifier || Access.colorFilter || Access.stickyKeys
                        menu: true
                        onDetails: root.showPage("accessibility")
                        onContextMenu: root.openSettings("accessibility-settings")
                    }
                }

                Slider {
                    visible: Status.brightnessAvailable
                    icon: "brightness-high"
                    value: Status.brightness
                    onMoved: value => Status.setBrightness(value)
                }
                Slider {
                    visible: Status.volumeAvailable
                    icon: Status.volumeIconName
                    value: Status.volume
                    onMoved: value => { if (Status.muted) Status.setMuted(false); Status.setVolume(value) }
                    onIconClicked: Status.setMuted(!Status.muted)
                }
            }

            // ------------------------------------------------- reti Wi-Fi --
            Column {
                id: wifiPage
                visible: root.page === "wifi"
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 8
                property string expanded: "" // la rete aperta
                property string connecting: "" // la rete a cui ci si sta connettendo
                property string password: ""

                PageHeader {
                    title: "Wi-Fi"
                    Switch {
                        checked: Status.wifiEnabled
                        onToggled: Status.setWifiEnabled(!Status.wifiEnabled)
                    }
                }
                Text {
                    visible: !Status.wifiEnabled || Network.wifiNetworks.length === 0
                    width: parent.width
                    topPadding: 8
                    bottomPadding: 8
                    text: !Status.wifiEnabled ? "Il Wi-Fi è spento."
                        : Network.scanning ? "Ricerca delle reti in corso..." : "Nessuna rete trovata."
                    color: Theme.textDim
                    font.pixelSize: Theme.fontNormal
                    wrapMode: Text.WordWrap
                }
                Flickable {
                    visible: Status.wifiEnabled && Network.wifiNetworks.length > 0
                    width: parent.width
                    height: Math.min(contentHeight, 340)
                    contentHeight: networks.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: networks
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: Network.wifiNetworks
                            delegate: Rectangle {
                                id: net
                                required property var modelData
                                readonly property bool open: wifiPage.expanded === modelData.ssid
                                readonly property bool needsPassword: modelData.secure && !modelData.known && !modelData.active
                                readonly property bool busy: wifiPage.connecting === modelData.ssid && Network.connectResult === ""
                                width: networks.width
                                height: open ? row.height + actions.height + 12 : row.height
                                radius: Theme.radiusSmall
                                color: open ? Theme.surfaceRaised : netMouse.containsMouse ? Theme.hover : "transparent"
                                clip: true

                                Item {
                                    id: row
                                    width: parent.width
                                    height: 52
                                    Image {
                                        x: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 20
                                        height: 20
                                        source: Theme.icons + (net.modelData.signal > 75 ? "network-wireless-signal-excellent"
                                            : net.modelData.signal > 50 ? "network-wireless-signal-good"
                                            : net.modelData.signal > 25 ? "network-wireless-signal-ok" : "network-wireless-signal-weak")
                                        sourceSize: Qt.size(width, height)
                                    }
                                    Column {
                                        x: 44
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 56
                                        Text {
                                            width: parent.width
                                            text: net.modelData.ssid
                                            color: Theme.text
                                            font.pixelSize: Theme.fontNormal
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: net.busy ? "Connessione in corso..."
                                                : net.modelData.active ? (net.modelData.secure ? "Connesso, protetta" : "Connesso")
                                                : net.modelData.secure ? "Protetta" : "Aperta"
                                            color: Theme.textDim
                                            font.pixelSize: Theme.fontSmall
                                        }
                                    }
                                    MouseArea {
                                        id: netMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            wifiPage.expanded = net.open ? "" : net.modelData.ssid
                                            wifiPage.password = ""
                                        }
                                    }
                                }

                                Column {
                                    id: actions
                                    visible: net.open
                                    x: 44
                                    y: row.height
                                    width: parent.width - 56
                                    spacing: 8

                                    // La password, per le reti protette che non conosciamo ancora.
                                    Rectangle {
                                        visible: net.needsPassword
                                        width: parent.width
                                        height: 32
                                        radius: Theme.radiusSmall
                                        color: Theme.surfaceRaised
                                        border.width: 1
                                        border.color: passwordField.activeFocus ? Theme.accent : Theme.stroke
                                        MenuTextField {
                                            id: passwordField
                                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                            verticalAlignment: TextInput.AlignVCenter
                                            echoMode: TextInput.Password
                                            text: wifiPage.password
                                            onTextEdited: wifiPage.password = text
                                            onAccepted: connectButton.activate()
                                            mapToScreen: (x, y) => root.screenPoint(passwordField, x, y)
                                        }
                                        Text {
                                            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                                            visible: passwordField.text === ""
                                            text: "Immetti la chiave di sicurezza della rete"
                                            color: Theme.textDim
                                            font.pixelSize: Theme.fontSmall
                                        }
                                    }
                                    Text {
                                        visible: wifiPage.connecting === net.modelData.ssid && Network.connectResult !== "" && Network.connectResult !== "ok"
                                        width: parent.width
                                        text: Network.connectResult
                                        color: Theme.light ? "#c42b1c" : "#ff99a4"
                                        font.pixelSize: Theme.fontSmall
                                        wrapMode: Text.WordWrap
                                    }
                                    Rectangle {
                                        id: connectButton
                                        anchors.right: parent.right
                                        width: 120
                                        height: 32
                                        radius: Theme.radiusSmall
                                        readonly property bool primary: !net.modelData.active
                                        readonly property bool usable: !net.busy && (!net.needsPassword || wifiPage.password.length >= 8)
                                        opacity: usable ? 1 : 0.5
                                        color: primary ? (buttonMouse.containsMouse ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
                                            : buttonMouse.containsMouse ? Theme.hover : Theme.surfaceRaised
                                        border.width: primary ? 0 : 1
                                        border.color: Theme.stroke
                                        function activate() {
                                            if (!usable) return
                                            if (net.modelData.active) {
                                                Network.disconnectWifi(net.modelData.ssid)
                                            } else {
                                                wifiPage.connecting = net.modelData.ssid
                                                Network.connectWifi(net.modelData.ssid, net.needsPassword ? wifiPage.password : "")
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: net.modelData.active ? "Disconnetti" : net.needsPassword ? "Avanti" : "Connetti"
                                            color: connectButton.primary ? "white" : Theme.text
                                            font.pixelSize: Theme.fontNormal
                                        }
                                        MouseArea {
                                            id: buttonMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: connectButton.activate()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Item { width: 1; height: 4 }
                Link {
                    text: "Altre impostazioni Wi-Fi"
                    target: "network"
                }
            }

            // ------------------------------------------------ accessibilità --
            Column {
                id: accessPage
                visible: root.page === "accessibility"
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 4

                PageHeader { title: "Accessibilità" }
                Item { width: 1; height: 4 }
                Repeater {
                    model: [
                        { icon: "zoom-in", text: "Lente di ingrandimento", key: "magnifier" },
                        { icon: "preferences-desktop-color", text: "Filtri colore", key: "colorFilter" },
                        { icon: "input-keyboard", text: "Tasti permanenti", key: "stickyKeys" }
                    ]
                    delegate: Rectangle {
                        id: option
                        required property var modelData
                        width: accessPage.width
                        height: 48
                        radius: Theme.radiusSmall
                        color: optionMouse.containsMouse ? Theme.hover : "transparent"
                        Image {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            source: Theme.icons + option.modelData.icon
                            sourceSize: Qt.size(width, height)
                        }
                        Text {
                            x: 44
                            anchors.verticalCenter: parent.verticalCenter
                            text: option.modelData.text
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                        }
                        MouseArea {
                            id: optionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: Access[option.modelData.key] = !Access[option.modelData.key]
                        }
                        Switch {
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            checked: Access[option.modelData.key]
                            onToggled: Access[option.modelData.key] = !Access[option.modelData.key]
                        }
                    }
                }
                Item { width: 1; height: 8 }
                Link {
                    text: "Altre impostazioni di accessibilità"
                    target: "accessibility-settings"
                }
            }
        }

        // In fondo: batteria a sinistra, Impostazioni a destra.
        Rectangle {
            id: footer
            visible: root.page === ""
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 48
            color: Theme.footer
            bottomLeftRadius: Theme.radiusMenu - 1
            bottomRightRadius: Theme.radiusMenu - 1

            Row {
                visible: Status.batteryPresent
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 8
                Image {
                    width: 18
                    height: 18
                    source: Theme.icons + encodeURIComponent(Status.batteryCharging ? "battery-good-charging" : "battery-good")
                    sourceSize: Qt.size(width, height)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Status.batteryPercent + "%"
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                }
            }
            Item {
                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                width: 36
                height: 36
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: Theme.hover
                    opacity: settingsMouse.containsMouse ? 1 : 0
                }
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    source: Theme.icons + "configure"
                    sourceSize: Qt.size(width, height)
                }
                MouseArea {
                    id: settingsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.openSettings("settings")
                }
            }
        }
    }
}
