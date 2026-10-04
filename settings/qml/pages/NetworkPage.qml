import QtQuick

// Rete e Internet: com'è collegato il PC, le reti Wi-Fi intorno e le
// proprietà di ogni collegamento (indirizzi, DNS), come Windows 11.
Page {
    id: page
    Component.onCompleted: {
        Network.refresh()
        if (Status.wifiAvailable && Status.wifiEnabled) {
            Network.scan()
        }
    }
    readonly property var wifiDevice: Network.devices.find(d => d.type === "wifi")
    readonly property var ethernetDevices: Network.devices.filter(d => d.type === "ethernet")
    property string expanded: "" // il dispositivo con le proprietà aperte

    function stateText(d) {
        if (d.state.indexOf("connected") === 0 && d.state.indexOf("disconnected") < 0) return "Connesso"
        if (d.state === "unavailable") return d.type === "ethernet" ? "Cavo scollegato" : "Non disponibile"
        if (d.state.indexOf("connecting") >= 0) return "Connessione in corso..."
        return "Disconnesso"
    }

    // In cima: lo stato, grande.
    Item {
        width: parent.width
        height: 96
        Image {
            id: statusIcon
            anchors.verticalCenter: parent.verticalCenter
            width: 64
            height: 64
            source: "image://icon/" + encodeURIComponent(Status.networkIconName)
            sourceSize: Qt.size(width, height)
        }
        Column {
            anchors { left: statusIcon.right; leftMargin: 16; verticalCenter: parent.verticalCenter }
            Text {
                text: Status.networkConnected ? (Status.networkName !== "" ? Status.networkName : "Connesso") : "Non connesso"
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.weight: Font.DemiBold
            }
            Text {
                text: Status.networkConnected ? "Connesso a Internet" : "Nessuna connessione"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
    }

    Card {
        visible: !Network.available
        icon: "dialog-warning"
        title: "NetworkManager non disponibile"
        description: "Serve nmcli per vedere e cambiare le connessioni."
    }

    // --- Wi-Fi ---
    Card {
        visible: Status.wifiAvailable
        icon: "network-wireless"
        title: "Wi-Fi"
        description: Status.wifiEnabled ? (page.wifiDevice && page.wifiDevice.connection !== "" ? "Connesso a " + page.wifiDevice.connection : "Non connesso") : "Disattivato"
        trailing: Toggle {
            checked: Status.wifiEnabled
            onToggled: on => {
                Status.setWifiEnabled(on)
                if (on) scanLater.start()
            }
        }
        Timer { id: scanLater; interval: 2000; onTriggered: Network.scan() }
    }
    Card {
        visible: Status.wifiAvailable && Status.wifiEnabled
        icon: "view-refresh"
        title: "Reti disponibili"
        description: Network.scanning ? "Ricerca in corso..." : Network.wifiNetworks.length + " reti"
        trailing: Button {
            text: "Aggiorna"
            usable: !Network.scanning
            onClicked: Network.scan()
        }
        contentItem: Column {
            width: parent.width
            spacing: 2
            Repeater {
                model: Network.wifiNetworks
                delegate: Rectangle {
                    id: net
                    required property var modelData
                    width: parent.width
                    height: 48
                    radius: Theme.radius
                    color: netMouse.containsMouse ? Theme.subtleHover : "transparent"
                    Image {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        source: "image://icon/" + (net.modelData.signal > 75 ? "network-wireless-signal-excellent"
                            : net.modelData.signal > 50 ? "network-wireless-signal-good"
                            : net.modelData.signal > 25 ? "network-wireless-signal-ok" : "network-wireless-signal-weak")
                        sourceSize: Qt.size(width, height)
                    }
                    Column {
                        x: 36
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: net.modelData.ssid
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                        }
                        Text {
                            text: (net.modelData.active ? "Connesso, " : "") + (net.modelData.secure ? "protetta" : "aperta")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                    Row {
                        anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                        spacing: 8
                        Button {
                            visible: net.modelData.known && !net.modelData.active
                            subtle: true
                            text: "Dimentica"
                            onClicked: Network.forget(net.modelData.ssid)
                        }
                        Button {
                            text: net.modelData.active ? "Disconnetti" : "Connetti"
                            onClicked: {
                                if (net.modelData.active) {
                                    Network.disconnectDevice(page.wifiDevice.device)
                                } else if (net.modelData.secure && !net.modelData.known) {
                                    passwordDialog.ssid = net.modelData.ssid
                                    password.text = ""
                                    passwordDialog.open()
                                    password.forceActiveFocus()
                                } else {
                                    Network.connectWifi(net.modelData.ssid, "")
                                }
                            }
                        }
                    }
                    MouseArea {
                        id: netMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
            }
        }
    }
    Text {
        visible: Network.connectResult !== "" && Network.connectResult !== "ok"
        width: parent.width
        text: Network.connectResult
        color: Theme.critical
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
        topPadding: 4
        bottomPadding: 4
    }

    // --- Ethernet (e le proprietà di ogni collegamento) ---
    Repeater {
        model: Network.devices
        delegate: Card {
            id: deviceCard
            required property var modelData
            icon: modelData.type === "wifi" ? "network-wireless" : "network-wired"
            title: (modelData.type === "wifi" ? "Wi-Fi" : "Ethernet") + (Network.devices.filter(d => d.type === modelData.type).length > 1 ? " (" + modelData.device + ")" : "")
            description: page.stateText(modelData) + (modelData.connection !== "" && modelData.type === "ethernet" ? " · " + modelData.connection : "")
            visible: modelData.type === "ethernet" || (modelData.type === "wifi" && modelData.ip !== undefined)
            clickable: true
            onClicked: page.expanded = page.expanded === modelData.device ? "" : modelData.device
            trailing: Text {
                text: page.expanded === deviceCard.modelData.device ? "⌃" : "⌄"
                color: Theme.textSecondary
                font.pixelSize: 14
            }
            contentItem: Column {
                visible: page.expanded === deviceCard.modelData.device
                width: parent.width
                spacing: 6
                Repeater {
                    model: [
                        { label: "Indirizzo IPv4", value: deviceCard.modelData.ip || "—" },
                        { label: "Gateway", value: deviceCard.modelData.gateway || "—" },
                        { label: "Server DNS", value: deviceCard.modelData.dns || "—" },
                        { label: "Indirizzo IPv6", value: deviceCard.modelData.ipv6 || "—" },
                        { label: "Indirizzo fisico (MAC)", value: deviceCard.modelData.mac || "—" },
                        { label: "Interfaccia", value: deviceCard.modelData.device }
                    ]
                    delegate: Row {
                        required property var modelData
                        width: parent.width
                        Text {
                            width: 220
                            leftPadding: 36
                            text: modelData.label
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                        }
                        Text {
                            width: parent.width - 220
                            text: modelData.value
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }

    CardGroup {
        title: "Impostazioni di rete avanzate"
        LinkCard {
            visible: System.available("network")
            icon: "preferences-system-network"
            title: "Connessioni"
            description: "Indirizzi statici, VPN, proxy e tutte le opzioni delle connessioni"
            onClicked: System.trigger("network")
        }
    }

    Dialog {
        id: passwordDialog
        parent: root.contentItem
        property string ssid
        title: "Connetti a " + ssid
        primaryText: "Avanti"
        primaryEnabled: password.text.length >= 8
        onAccepted: Network.connectWifi(ssid, password.text)
        Column {
            width: parent.width
            spacing: 8
            Text {
                text: "Immetti la chiave di sicurezza di rete"
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }
            TextBox {
                id: password
                width: parent.width
                echoMode: TextInput.Password
                Keys.onReturnPressed: if (passwordDialog.primaryEnabled) { passwordDialog.close(); Network.connectWifi(passwordDialog.ssid, text) }
            }
        }
    }
}
