import QtQuick
import QtQuick.Shapes

// Le impostazioni rapide (Win+A, o clic sulle icone di sistema della
// taskbar), come Windows 11: riquadri da accendere e spegnere (Wi-Fi,
// Bluetooth, modalità aereo, risparmio energia...), luminosità e volume,
// batteria e Impostazioni in fondo. Pannello acrylic in basso a destra.
Window {
    id: root
    objectName: "quickSettings"
    visible: false
    width: 384
    height: panel.height + 24
    color: "transparent"

    function open() {
        Menus.notificationCenterOpen = false
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

    // Un riquadro: acceso (colore d'accento) o spento, con l'etichetta sotto.
    component Tile: Item {
        id: tile
        property string icon
        property string label
        property bool checked
        property bool usable: true
        signal toggled()
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
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "white"
                opacity: tileMouse.pressed ? 0.06 : tileMouse.containsMouse && tile.usable ? 0.08 : 0
            }
            Image {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: "image://icon/" + encodeURIComponent(tile.icon)
                sourceSize: Qt.size(width, height)
            }
            MouseArea {
                id: tileMouse
                anchors.fill: parent
                hoverEnabled: true
                enabled: tile.usable
                onClicked: tile.toggled()
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
                source: "image://icon/" + encodeURIComponent(slider.icon)
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

    Rectangle {
        id: panel
        x: 12
        y: 12
        width: root.width - 24
        height: content.height + 24 + footer.height
        radius: Theme.radiusMenu
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
        transform: Translate { id: slide }
        focus: true
        Keys.onEscapePressed: root.close()
        onHeightChanged: root.updateBlur()

        Column {
            id: content
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
                    onToggled: Status.setWifiEnabled(!Status.wifiEnabled)
                }
                // Senza Wi-Fi (un fisso col cavo): la rete com'è, a titolo informativo.
                Tile {
                    visible: !Status.wifiAvailable
                    icon: Status.networkIconName
                    label: Status.networkConnected ? (Status.networkName !== "" ? Status.networkName : "Rete") : "Non connesso"
                    checked: Status.networkConnected
                    onToggled: System.trigger("network")
                }
                Tile {
                    visible: Status.bluetoothAvailable
                    icon: Status.bluetoothEnabled ? "network-bluetooth" : "network-bluetooth-inactive"
                    label: "Bluetooth"
                    checked: Status.bluetoothEnabled
                    onToggled: Status.setBluetoothEnabled(!Status.bluetoothEnabled)
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
                }
                Tile {
                    icon: "redshift-status-on"
                    label: "Luce notturna"
                    usable: false
                }
                Tile {
                    icon: "preferences-desktop-accessibility"
                    label: "Accessibilità"
                    usable: false
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

        // In fondo: batteria a sinistra, Impostazioni a destra.
        Rectangle {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 48
            color: Qt.rgba(0, 0, 0, 0.18)
            bottomLeftRadius: Theme.radiusMenu - 1
            bottomRightRadius: Theme.radiusMenu - 1

            Row {
                visible: Status.batteryPresent
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 8
                Image {
                    width: 18
                    height: 18
                    source: "image://icon/" + encodeURIComponent(Status.batteryCharging ? "battery-good-charging" : "battery-good")
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
                    source: "image://icon/configure"
                    sourceSize: Qt.size(width, height)
                }
                MouseArea {
                    id: settingsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.close()
                        System.trigger("settings")
                    }
                }
            }
        }
    }
}
