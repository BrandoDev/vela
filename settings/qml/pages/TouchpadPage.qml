import QtQuick

// Bluetooth e dispositivi > Touchpad, come Windows 11: acceso o spento
// (anche solo quando c'è un mouse), velocità, tocchi, direzione dello
// scorrimento e gesti a tre e quattro dita. Le applica il compositor.
Page {
    id: page
    readonly property var actions: ["app", "desktop", "no"]
    readonly property var actionNames: ["Cambia app e mostra il desktop", "Cambia desktop e mostra il desktop", "Niente"]

    Card {
        visible: !Prefs.hasTouchpad
        icon: "input-touchpad"
        title: "Nessun touchpad"
        description: "Queste impostazioni valgono quando c'è un touchpad, per esempio su un portatile."
    }

    CardGroup {
        Card {
            icon: "input-touchpad"
            title: "Touchpad"
            trailing: Toggle {
                checked: Prefs.touchpad
                onToggled: on => Prefs.touchpad = on
            }
        }
        Card {
            icon: "input-mouse"
            title: "Lascia attivo il touchpad quando è collegato un mouse"
            trailing: Toggle {
                checked: Prefs.touchpadWithMouse
                onToggled: on => Prefs.touchpadWithMouse = on
            }
        }
        Card {
            icon: "transform-move"
            title: "Velocità del cursore"
            trailing: Slider {
                width: 220
                from: 1
                to: 20
                stepSize: 1
                live: false
                value: Prefs.touchpadSpeed
                onReleased: value => Prefs.touchpadSpeed = Math.round(value)
            }
        }
    }

    CardGroup {
        title: "Tocchi e scorrimento"
        Card {
            icon: "input-touchpad"
            title: "Tocca con un dito per fare un solo clic"
            description: "Con due dita il clic destro, con un tocco e trascinando si sposta"
            trailing: Toggle {
                checked: Prefs.touchpadTap
                onToggled: on => Prefs.touchpadTap = on
            }
        }
        Card {
            icon: "transform-move-vertical"
            title: "Direzione di scorrimento"
            description: "Con due dita: muovendole verso il basso"
            trailing: Choice {
                model: ["Il contenuto sale", "Il contenuto scende"]
                currentIndex: Prefs.touchpadNatural ? 0 : 1
                onChosen: index => Prefs.touchpadNatural = index === 0
            }
        }
    }

    CardGroup {
        title: "Gesti"
        Card {
            icon: "gesture"
            title: "Gesti con tre dita"
            description: "Verso l'alto: Visualizzazione attività; verso il basso: il desktop; di lato:"
            trailing: Choice {
                model: page.actionNames
                currentIndex: Math.max(0, page.actions.indexOf(Prefs.threeFingers))
                onChosen: index => Prefs.threeFingers = page.actions[index]
            }
        }
        Card {
            icon: "gesture"
            title: "Gesti con quattro dita"
            description: "Verso l'alto: Visualizzazione attività; verso il basso: il desktop; di lato:"
            trailing: Choice {
                model: page.actionNames
                currentIndex: Math.max(0, page.actions.indexOf(Prefs.fourFingers))
                onChosen: index => Prefs.fourFingers = page.actions[index]
            }
        }
    }
}
