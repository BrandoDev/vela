import QtQuick

// Bluetooth e dispositivi > Mouse, come Windows 11: pulsante principale,
// velocità del puntatore, "Migliora precisione puntatore", righe per ogni
// scatto della rotellina. Le applica il compositor (vela.conf).
Page {
    CardGroup {
        Card {
            icon: "input-mouse"
            title: "Pulsante del mouse principale"
            trailing: Choice {
                model: ["Sinistro", "Destro"]
                currentIndex: Prefs.mouseLeftHanded ? 1 : 0
                onChosen: index => Prefs.mouseLeftHanded = index === 1
            }
        }
        Card {
            icon: "transform-move"
            title: "Velocità del puntatore del mouse"
            trailing: Slider {
                width: 220
                from: 1
                to: 20
                stepSize: 1
                live: false
                value: Prefs.mouseSpeed
                onReleased: value => Prefs.mouseSpeed = Math.round(value)
            }
        }
        Card {
            icon: "edit-select"
            title: "Migliora precisione puntatore"
            description: "Il puntatore va più lontano quando muovi il mouse più in fretta (accelerazione)"
            trailing: Toggle {
                checked: Prefs.mousePrecision
                onToggled: on => Prefs.mousePrecision = on
            }
        }
    }

    CardGroup {
        title: "Scorrimento"
        Card {
            icon: "input-mouse"
            title: "Righe da scorrere ogni volta"
            description: "Per ogni scatto della rotellina"
            trailing: Slider {
                width: 220
                from: 1
                to: 20
                stepSize: 1
                live: false
                value: Prefs.wheelLines
                onReleased: value => Prefs.wheelLines = Math.round(value)
            }
        }
    }

    CardGroup {
        title: "Impostazioni correlate"
        LinkCard {
            visible: Prefs.hasTouchpad
            icon: "input-touchpad"
            title: "Touchpad"
            description: "Tocchi, gesti, scorrimento"
            onClicked: root.navigate("touchpad")
        }
        LinkCard {
            icon: "preferences-desktop-accessibility"
            title: "Accessibilità"
            description: "Lente di ingrandimento, filtri colore, tasti permanenti"
            onClicked: root.navigate("accessibility")
        }
    }
}
