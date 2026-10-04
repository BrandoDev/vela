import QtQuick

// La Home di Windows 11: il dispositivo in cima, poi le schede più usate.
Page {
    // Il dispositivo: sfondo in miniatura, nome, Rinomina.
    Item {
        width: parent.width
        height: 140
        Rectangle {
            id: preview
            width: 186
            height: 116
            radius: Theme.radiusCard
            color: "black"
            clip: true
            Image {
                anchors.fill: parent
                anchors.margins: 4
                source: Prefs.wallpaper.startsWith(":") ? "qrc" + Prefs.wallpaper : "file://" + Prefs.wallpaper
                sourceSize: Qt.size(356, 216)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }
        Column {
            anchors { left: preview.right; leftMargin: 20; verticalCenter: preview.verticalCenter }
            spacing: 2
            Text {
                text: About.hostname
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.weight: Font.DemiBold
            }
            Text {
                text: About.model
                visible: text !== ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
            Text {
                text: "Rinomina"
                color: Theme.accentFill
                font.pixelSize: Theme.fontBody
                topPadding: 4
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.navigate("about")
                }
            }
        }
    }

    CardGroup {
        title: "Impostazioni consigliate"
        LinkCard {
            icon: "preferences-desktop-wallpaper"
            title: "Sfondo"
            description: "Immagini, colori e foto per il desktop"
            onClicked: root.navigate("background")
        }
        LinkCard {
            icon: "preferences-desktop-color"
            title: "Colori"
            description: "Colore principale, modalità chiara o scura per le app"
            onClicked: root.navigate("colors")
        }
        LinkCard {
            icon: "video-display"
            title: "Schermo"
            description: "Risoluzione, scala, frequenza di aggiornamento"
            onClicked: root.navigate("display")
        }
    }

    CardGroup {
        title: "Dispositivi Bluetooth"
        visible: Status.bluetoothAvailable
        Card {
            icon: "preferences-system-bluetooth"
            title: "Bluetooth"
            description: Status.bluetoothEnabled ? "Individuabile come \"" + About.hostname + "\"" : "Disattivato"
            trailing: Toggle {
                checked: Status.bluetoothEnabled
                onToggled: on => Status.setBluetoothEnabled(on)
            }
        }
        LinkCard {
            icon: "list-add"
            title: "Visualizza tutti i dispositivi"
            onClicked: root.navigate("bluetooth")
        }
    }

    CardGroup {
        title: "Connessione"
        LinkCard {
            icon: Status.networkIconName
            title: Status.networkConnected ? (Status.networkName !== "" ? Status.networkName : "Connesso") : "Non connesso"
            description: Status.networkConnected ? (Status.networkWireless ? "Wi-Fi · Connesso, protetto" : "Ethernet · Connesso") : "Nessuna connessione a Internet"
            onClicked: root.navigate("network")
        }
    }
}
