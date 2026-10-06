// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Sistema > Appunti, come Windows 11: la cronologia degli Appunti (Win+V)
// e "Cancella dati degli Appunti" (tranne gli elementi fissati).
Page {
    CardGroup {
        Card {
            icon: "edit-paste"
            title: "Cronologia degli Appunti"
            description: "Salva più elementi negli Appunti da usare in seguito. Premi Win+V per vederli e incollarli"
            trailing: Toggle {
                checked: Prefs.clipboardHistory
                onToggled: on => Prefs.clipboardHistory = on
            }
        }
        Card {
            icon: "edit-clear-history"
            title: "Cancella dati degli Appunti"
            description: "Tutto tranne gli elementi fissati"
            trailing: Button {
                text: "Cancella"
                onClicked: Prefs.clearClipboard()
            }
        }
    }
    CardGroup {
        title: "Impostazioni correlate"
        Card {
            icon: "accessories-screenshot"
            title: "Strumento di cattura"
            description: "Win+Maiusc+S o Stamp: un rettangolo, una finestra o lo schermo intero, copiati negli Appunti e salvati in Immagini > Screenshot"
        }
    }
}
