import QtQuick

// Sistema > Schermo > Luce notturna, come Windows 11: accenderla ora,
// quanto deve essere calda, e la pianificazione (dal tramonto all'alba,
// con le ore del sole del fuso orario, oppure dalle-alle).
Page {
    id: page
    // Le ore ogni quarto d'ora, per scegliere quando accenderla e spegnerla.
    readonly property var times: {
        const list = []
        for (let m = 0; m < 24 * 60; m += 15) {
            list.push(String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"))
        }
        return list
    }

    // Un'opzione a scelta singola (pallino).
    component Radio: Item {
        id: radio
        property string text
        property bool checked
        property bool usable: true
        signal clicked()
        width: parent.width
        height: 32
        opacity: usable ? 1 : 0.45
        Rectangle {
            id: dot
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            radius: 10
            color: radio.checked ? Theme.accentFill : Theme.control
            border.width: radio.checked ? 0 : 1
            border.color: Theme.controlStrokeStrong
            Rectangle {
                visible: radio.checked
                anchors.centerIn: parent
                width: radioMouse.containsMouse ? 10 : 8
                height: width
                radius: width / 2
                color: Theme.accentText
            }
        }
        Text {
            anchors { left: dot.right; leftMargin: 12; verticalCenter: parent.verticalCenter }
            text: radio.text
            color: Theme.text
            font.pixelSize: Theme.fontBody
        }
        MouseArea {
            id: radioMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: radio.usable
            onClicked: radio.clicked()
        }
    }

    CardGroup {
        Card {
            icon: "redshift-status-on"
            title: "Luce notturna"
            description: "Colori più caldi la sera, per affaticare meno la vista e aiutare il sonno"
            trailing: Button {
                text: Prefs.nightLight ? "Disattiva ora" : "Attiva ora"
                onClicked: Prefs.nightLight = !Prefs.nightLight
            }
        }
        Card {
            icon: "color-management"
            title: "Intensità"
            trailing: Slider {
                width: 220
                from: 0
                to: 100
                stepSize: 1
                live: false
                value: Prefs.nightStrength
                onReleased: value => Prefs.nightStrength = Math.round(value)
            }
        }
        Card {
            icon: "chronometer"
            title: "Pianifica luce notturna"
            trailing: Toggle {
                checked: Prefs.nightSchedule !== "no"
                onToggled: on => Prefs.nightSchedule = on ? (Prefs.sunset !== "" ? "tramonto" : "ore") : "no"
            }
            contentItem: Column {
                visible: Prefs.nightSchedule !== "no"
                x: 36
                width: parent.width - 36
                spacing: 4
                Radio {
                    text: "Dal tramonto all'alba" + (Prefs.sunset !== "" ? " (" + Prefs.sunset + " - " + Prefs.sunrise + ")" : "")
                    usable: Prefs.sunset !== ""
                    checked: Prefs.nightSchedule === "tramonto"
                    onClicked: Prefs.nightSchedule = "tramonto"
                }
                Radio {
                    text: "Imposta ore"
                    checked: Prefs.nightSchedule === "ore"
                    onClicked: Prefs.nightSchedule = "ore"
                }
                Row {
                    visible: Prefs.nightSchedule === "ore"
                    x: 32
                    spacing: 12
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Attiva"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    Choice {
                        model: page.times
                        currentIndex: Math.max(0, page.times.indexOf(Prefs.nightFrom))
                        onChosen: index => Prefs.nightFrom = page.times[index]
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Disattiva"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    Choice {
                        model: page.times
                        currentIndex: Math.max(0, page.times.indexOf(Prefs.nightTo))
                        onChosen: index => Prefs.nightTo = page.times[index]
                    }
                }
            }
        }
    }
}
