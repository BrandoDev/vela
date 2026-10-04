import QtQuick

// Ora e lingua > Data e ora: l'ora com'è adesso, la sincronizzazione
// automatica e il fuso orario (systemd-timedated).
Page {
    id: page
    property date now: new Date()
    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: page.now = new Date()
    }
    readonly property var zones: DateTime.timezones

    Item {
        width: parent.width
        height: 88
        Column {
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: Qt.formatTime(page.now, "HH:mm")
                color: Theme.text
                font.pixelSize: 40
                font.weight: Font.DemiBold
            }
            Text {
                text: {
                    const s = Qt.locale().toString(page.now, "dddd d MMMM yyyy")
                    return s.charAt(0).toUpperCase() + s.slice(1)
                }
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
    }

    CardGroup {
        Card {
            icon: "chronometer"
            title: "Imposta automaticamente l'ora"
            description: DateTime.canNtp ? "Sincronizza l'orologio con un server dell'ora su Internet" : "Nessun servizio di sincronizzazione installato"
            trailing: Toggle {
                usable: DateTime.canNtp
                checked: DateTime.ntp
                onToggled: on => DateTime.setNtp(on)
            }
        }
        Card {
            icon: "preferences-system-time"
            title: "Fuso orario"
            trailing: Choice {
                implicitWidth: 320
                model: page.zones.map(z => ({ text: DateTime.describe(z) }))
                currentIndex: page.zones.indexOf(DateTime.timezone)
                onChosen: index => DateTime.setTimezone(page.zones[index])
            }
        }
    }

    CardGroup {
        title: "Impostazioni correlate"
        LinkCard {
            visible: System.available("datetime")
            icon: "preferences-system-time"
            title: "Altre impostazioni dell'orologio"
            description: "Impostare data e ora a mano, orologio hardware"
            onClicked: System.trigger("datetime")
        }
    }
}
