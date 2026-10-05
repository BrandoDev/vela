import QtQuick

// Personalizzazione > Barra delle applicazioni.
Page {
    PersonalizationPage.DesktopPreview {}

    CardGroup {
        title: "Elementi della barra delle applicazioni"
        Card {
            icon: "view-grid"
            title: "Visualizzazione attività"
            description: "Le finestre aperte e i desktop virtuali (anche con Win+Tab)"
            trailing: Toggle {
                checked: Prefs.taskView
                onToggled: on => Prefs.taskView = on
            }
        }
    }

    CardGroup {
        title: "Comportamenti della barra delle applicazioni"
        Card {
            icon: "format-justify-center"
            title: "Allineamento della barra delle applicazioni"
            trailing: Choice {
                model: ["Al centro", "A sinistra"]
                currentIndex: Prefs.taskbarAlignment === "left" ? 1 : 0
                onChosen: index => Prefs.taskbarAlignment = index === 1 ? "left" : "center"
            }
        }
        Card {
            icon: "video-display"
            title: "Mostra la barra delle applicazioni su tutti gli schermi"
            description: "Con più monitor: Start, le app e l'orologio su ognuno; area di notifica sul principale"
            trailing: Toggle {
                checked: Prefs.taskbarAllScreens
                onToggled: on => Prefs.taskbarAllScreens = on
            }
        }
        Card {
            icon: "process-stop"
            title: "Termina attività"
            description: "Nel menu dei pulsanti della barra delle applicazioni, per chiudere subito un'app che non risponde"
            trailing: Toggle {
                checked: Prefs.endTask
                onToggled: on => Prefs.endTask = on
            }
        }
    }
}
