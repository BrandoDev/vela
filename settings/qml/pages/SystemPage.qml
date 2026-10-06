import QtQuick

Page {
    CardGroup {
        LinkCard {
            icon: "video-display"
            title: "Schermo"
            description: "Monitor, luminosità, risoluzione, scala"
            onClicked: root.navigate("display")
        }
        LinkCard {
            icon: "audio-speakers"
            title: "Audio"
            description: "Livelli del volume, output, input, dispositivi audio"
            onClicked: root.navigate("sound")
        }
        LinkCard {
            icon: "preferences-desktop-notification"
            title: "Notifiche"
            description: "Avvisi da app e sistema, Non disturbare"
            onClicked: root.navigate("notifications")
        }
        LinkCard {
            icon: "preferences-system-power-management"
            title: "Alimentazione"
            description: Status.batteryPresent ? "Sospensione, utilizzo della batteria, risparmio energia" : "Schermo e sospensione, modalità di risparmio energia"
            onClicked: root.navigate("power")
        }
        LinkCard {
            icon: "edit-paste"
            title: "Appunti"
            description: "Cronologia degli Appunti (Win+V), cancella"
            onClicked: root.navigate("clipboard")
        }
        LinkCard {
            icon: "help-about"
            title: "Informazioni"
            description: "Specifiche del dispositivo, rinomina il PC, specifiche del sistema operativo"
            onClicked: root.navigate("about")
        }
    }
}
