// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Sistema > Alimentazione: quando spegnere lo schermo, il blocco, la
// modalità di risparmio energia (power-profiles-daemon), la batteria.
Page {
    id: page
    readonly property var minutes: [1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 120, 180, 240, 300, 0]
    function label(m) {
        if (m === 0) return "Mai"
        if (m < 60) return m + (m === 1 ? " minuto" : " minuti")
        return (m / 60) + (m === 60 ? " ora" : " ore")
    }
    readonly property var profileNames: ({ "power-saver": "Risparmio energia", "balanced": "Bilanciata", "performance": "Prestazioni migliori" })
    readonly property var profiles: ["power-saver", "balanced", "performance"].filter(p => Status.powerProfiles.indexOf(p) >= 0)

    Card {
        visible: Status.batteryPresent
        icon: Status.batteryCharging ? "battery-good-charging" : "battery-good"
        title: Status.batteryPercent + "%"
        description: Status.batteryCharging ? "In carica" : "A batteria"
        minimumHeight: 80
    }

    CardGroup {
        title: "Alimentazione"
        Card {
            icon: "video-display"
            title: "Schermo e sospensione"
            description: "Dopo questo tempo senza usare mouse e tastiera lo schermo si spegne"
            trailing: Choice {
                model: page.minutes.map(m => page.label(m))
                currentIndex: Math.max(0, page.minutes.indexOf(Prefs.screenOffMinutes))
                onChosen: index => Prefs.screenOffMinutes = page.minutes[index]
            }
        }
        Card {
            icon: "system-lock-screen"
            title: "Blocca lo schermo quando si spegne"
            description: "Per tornare serve la password"
            trailing: Toggle {
                checked: Prefs.lockOnIdle
                onToggled: on => Prefs.lockOnIdle = on
            }
        }
        Card {
            visible: page.profiles.length > 1
            icon: "battery-profile-performance"
            title: "Modalità di alimentazione"
            description: "Ottimizza il dispositivo in base al consumo di energia e alle prestazioni"
            trailing: Choice {
                model: page.profiles.map(p => page.profileNames[p])
                currentIndex: page.profiles.indexOf(Status.powerProfile)
                onChosen: index => Status.powerProfile = page.profiles[index]
            }
        }
    }
}
