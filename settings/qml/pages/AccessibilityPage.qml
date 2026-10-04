import QtQuick

// Accessibilità, come Windows 11: la lente di ingrandimento, i filtri
// colore (anche con Win+Ctrl+C) e i tasti permanenti. Le applica il
// compositor (compositor/src/accessibility.cpp).
Page {
    id: page
    readonly property var steps: [25, 50, 100, 150, 200, 400]
    readonly property var filters: [
        { value: "grigi", text: "Scala di grigi" },
        { value: "deuteranopia", text: "Rosso-verde (verde debole, deuteranopia)" },
        { value: "protanopia", text: "Rosso-verde (rosso debole, protanopia)" },
        { value: "tritanopia", text: "Blu-giallo (tritanopia)" }
    ]

    CardGroup {
        title: "Vista"
        Card {
            icon: "zoom-in"
            title: "Lente di ingrandimento"
            description: "Win+più apre la lente e ingrandisce, Win+meno riduce, Win+Esc la chiude"
            trailing: Toggle {
                checked: Prefs.magnifier
                onToggled: on => Prefs.magnifier = on
            }
        }
        Card {
            icon: "zoom-in"
            title: "Incremento dello zoom"
            description: "Di quanto ingrandisce ogni pressione di Win+più"
            trailing: Choice {
                model: page.steps.map(s => s + "%")
                currentIndex: Math.max(0, page.steps.indexOf(Prefs.magnifierStep))
                onChosen: index => Prefs.magnifierStep = page.steps[index]
            }
        }
        Card {
            icon: "preferences-desktop-color"
            title: "Filtri colore"
            description: "Per vedere meglio foto e colori, o distinguere i colori con il daltonismo"
            trailing: Toggle {
                checked: Prefs.colorFilter
                onToggled: on => Prefs.colorFilter = on
            }
        }
        Card {
            icon: "color-management"
            title: "Filtro"
            trailing: Choice {
                model: page.filters.map(f => f.text)
                currentIndex: Math.max(0, page.filters.findIndex(f => f.value === Prefs.colorFilterKind))
                onChosen: index => Prefs.colorFilterKind = page.filters[index].value
            }
        }
        Card {
            icon: "input-keyboard"
            title: "Scelta rapida da tastiera per i filtri colore"
            description: "Win+Ctrl+C li accende e li spegne"
            trailing: Toggle {
                checked: Prefs.colorFilterShortcut
                onToggled: on => Prefs.colorFilterShortcut = on
            }
        }
    }

    CardGroup {
        title: "Interazione"
        Card {
            icon: "input-keyboard"
            title: "Tasti permanenti"
            description: "Maiusc, Ctrl, Alt e Win premuti e lasciati valgono per il tasto dopo: le scorciatoie si fanno un tasto alla volta. Premuti due volte restano bloccati"
            trailing: Toggle {
                checked: Prefs.stickyKeys
                onToggled: on => Prefs.stickyKeys = on
            }
        }
    }
}
