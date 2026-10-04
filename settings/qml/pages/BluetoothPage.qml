import QtQuick

// Bluetooth e dispositivi: l'interruttore, "Aggiungi dispositivo" e i
// dispositivi associati, come Windows 11.
Page {
    id: page
    Component.onCompleted: Bluetooth.refresh()
    Component.onDestruction: Bluetooth.stopDiscovery()

    function deviceIcon(icon) {
        // Le icone di BlueZ (audio-headset, input-mouse...) sono già nomi del tema.
        return icon !== "" ? icon : "preferences-system-bluetooth"
    }
    function status(d) {
        if (d.busy) return "Connessione in corso..."
        let s = d.connected ? "Connesso" : "Associato"
        if (d.battery >= 0) s += " · Batteria " + d.battery + "%"
        return s
    }

    Card {
        visible: !Bluetooth.available
        icon: "preferences-system-bluetooth-inactive"
        title: "Bluetooth non disponibile"
        description: "Non c'è un adattatore Bluetooth, o il servizio bluetooth non è attivo."
    }

    Card {
        visible: Bluetooth.available
        icon: Bluetooth.powered ? "preferences-system-bluetooth" : "preferences-system-bluetooth-inactive"
        title: "Bluetooth"
        description: Bluetooth.powered ? "Individuabile come \"" + Bluetooth.adapterName + "\"" : "Disattivato"
        minimumHeight: 80
        trailing: Toggle {
            checked: Bluetooth.powered
            onToggled: on => Bluetooth.powered = on
        }
    }

    Card {
        visible: Bluetooth.available
        icon: "list-add"
        title: "Dispositivi"
        description: "Mouse, tastiera, penna, audio, schermi e dock, altri dispositivi"
        trailing: Button {
            text: "Aggiungi dispositivo"
            usable: Bluetooth.powered
            onClicked: {
                addDialog.open()
                Bluetooth.startDiscovery()
            }
        }
    }

    Text {
        visible: Bluetooth.error !== ""
        width: parent.width
        text: Bluetooth.error
        color: Theme.critical
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
        topPadding: 8
        bottomPadding: 8
    }

    Repeater {
        model: Bluetooth.paired
        delegate: Card {
            id: deviceCard
            required property var modelData
            icon: page.deviceIcon(modelData.icon)
            title: modelData.name
            description: page.status(modelData)
            trailing: Row {
                spacing: 8
                Button {
                    text: deviceCard.modelData.connected ? "Disconnetti" : "Connetti"
                    usable: !deviceCard.modelData.busy && Bluetooth.powered
                    onClicked: deviceCard.modelData.connected ? Bluetooth.disconnectDevice(deviceCard.modelData.path)
                                                              : Bluetooth.connectDevice(deviceCard.modelData.path)
                }
                Button {
                    subtle: true
                    text: "Rimuovi"
                    onClicked: {
                        removeDialog.device = deviceCard.modelData
                        removeDialog.open()
                    }
                }
            }
        }
    }

    CardGroup {
        title: "Impostazioni correlate"
        LinkCard {
            icon: "audio-speakers"
            title: "Audio"
            description: "Dove va il suono dei dispositivi collegati"
            onClicked: root.navigate("sound")
        }
    }

    Dialog {
        id: removeDialog
        parent: root.contentItem
        property var device: null
        title: "Rimuovere il dispositivo?"
        primaryText: "Sì"
        secondaryText: "No"
        onAccepted: Bluetooth.removeDevice(device.path)
        Text {
            width: parent.width
            text: removeDialog.device ? "Per usare di nuovo " + removeDialog.device.name + " dovrai associarlo un'altra volta." : ""
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }

    // "Aggiungi un dispositivo": la ricerca, e un clic per associare.
    Dialog {
        id: addDialog
        parent: root.contentItem
        title: "Aggiungi un dispositivo"
        primaryText: ""
        secondaryText: "Annulla"
        dialogWidth: 520
        onRejected: Bluetooth.stopDiscovery()
        Column {
            width: parent.width
            spacing: 8
            Text {
                width: parent.width
                text: "Assicurati che il dispositivo sia acceso e individuabile. Selezionalo qui sotto per connetterlo."
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }
            Item {
                width: parent.width
                height: Math.min(320, Math.max(found.contentHeight, 80))
                ListView {
                    id: found
                    anchors.fill: parent
                    clip: true
                    spacing: 2
                    model: Bluetooth.found
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Rectangle {
                        id: candidate
                        required property var modelData
                        width: found.width
                        height: 56
                        radius: Theme.radius
                        color: candidateMouse.containsMouse ? Theme.subtleHover : "transparent"
                        Image {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            source: Theme.icons + encodeURIComponent(page.deviceIcon(candidate.modelData.icon))
                            sourceSize: Qt.size(width, height)
                        }
                        Column {
                            x: 52
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: candidate.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                            }
                            Text {
                                visible: candidate.modelData.busy
                                text: "Associazione in corso..."
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                        MouseArea {
                            id: candidateMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !candidate.modelData.busy
                            onClicked: Bluetooth.pairAndConnect(candidate.modelData.path)
                        }
                    }
                }
                Text {
                    visible: found.count === 0
                    anchors.centerIn: parent
                    text: Bluetooth.discovering ? "Ricerca dei dispositivi..." : "Nessun dispositivo trovato"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
            }
        }
    }
    // Associato: il dispositivo passa dai "trovati" agli associati e la finestra si chiude.
    Connections {
        target: Bluetooth
        property int pairedCount: Bluetooth.paired.length
        function onChanged() {
            if (addDialog.visible && Bluetooth.paired.length > pairedCount) {
                addDialog.close()
                Bluetooth.stopDiscovery()
            }
            pairedCount = Bluetooth.paired.length
        }
    }
}
