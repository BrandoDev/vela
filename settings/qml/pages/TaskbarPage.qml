import QtQuick

// Personalizzazione > Barra delle applicazioni.
Page {
    PersonalizationPage.DesktopPreview {}

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
