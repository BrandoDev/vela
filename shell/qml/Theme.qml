// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQuick

// Linguaggio visivo di Vela in un solo posto. Le curve e le durate sono le
// stesse del compositor (compositor/src/motion.hpp): finestre e shell si
// muovono nello stesso modo.
QtObject {
    // --- colori: tema scuro o chiaro ("Scegli la modalità" nelle Impostazioni) ---
    readonly property bool light: Config.shellTheme === "light"
    readonly property color desktop: "#06182d" // sotto lo sfondo, il suo blu più scuro
    // Con la sfocatura del compositor (docs/renderer.md §8.3) i pannelli sono
    // acrylic come su Windows 11: semitrasparenti sopra lo sfondo sfocato.
    // Senza, opachi.
    readonly property bool acrylic: Effects.blurAvailable
    readonly property color taskbar: light
        ? (acrylic ? Qt.rgba(0.95, 0.95, 0.96, 0.66) : Qt.rgba(0.94, 0.94, 0.95, 0.96))
        : (acrylic ? Qt.rgba(0.11, 0.11, 0.13, 0.62) : Qt.rgba(0.10, 0.10, 0.12, 0.94))
    readonly property color surface: light
        ? (acrylic ? Qt.rgba(0.96, 0.96, 0.97, 0.74) : Qt.rgba(0.95, 0.95, 0.96, 0.98))
        : (acrylic ? Qt.rgba(0.14, 0.14, 0.16, 0.72) : Qt.rgba(0.14, 0.14, 0.16, 0.97))
    // Finestre di dialogo (Esegui, Proprietà, conferme): sempre piene.
    readonly property color dialog: light ? Qt.rgba(0.976, 0.976, 0.98, 0.99) : Qt.rgba(0.14, 0.14, 0.16, 0.98)
    readonly property color surfaceRaised: light ? Qt.rgba(1, 1, 1, 0.70) : Qt.rgba(1, 1, 1, 0.06)
    readonly property color popup: light
        ? (acrylic ? Qt.rgba(0.98, 0.98, 0.98, 0.82) : "#f9f9f9")
        : (acrylic ? Qt.rgba(0.17, 0.17, 0.19, 0.78) : "#2a2a2f") // menu
    readonly property color hover: light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.08)
    readonly property color pressed: light ? Qt.rgba(0, 0, 0, 0.03) : Qt.rgba(1, 1, 1, 0.05)
    readonly property color stroke: light ? Qt.rgba(0, 0, 0, 0.09) : Qt.rgba(1, 1, 1, 0.09)
    readonly property color text: light ? "#1b1b1b" : "#f3f3f6"
    readonly property color textDim: light ? "#5d5d66" : "#a4a4ae"
    // Per schiarire (o scurire, in tema chiaro) ciò che sta sotto: hover sui
    // riquadri colorati, fondi delle zone in un pannello.
    readonly property color overlay: light ? "black" : "white"
    readonly property color footer: light ? Qt.rgba(0, 0, 0, 0.035) : Qt.rgba(0, 0, 0, 0.18)
    // Pannelli scuri sopra lo schermo (Visualizzazione attività, Snap Assist).
    readonly property color backdrop: light
        ? (acrylic ? Qt.rgba(0.93, 0.93, 0.95, 0.55) : Qt.rgba(0.92, 0.92, 0.94, 0.96))
        : (acrylic ? Qt.rgba(0.05, 0.05, 0.07, 0.55) : Qt.rgba(0.07, 0.07, 0.09, 0.96))
    readonly property color card: light ? Qt.rgba(0.98, 0.98, 0.99, 0.92) : Qt.rgba(0.15, 0.15, 0.17, 0.92)
    readonly property color cardHover: light ? Qt.rgba(1, 1, 1, 0.97) : Qt.rgba(0.20, 0.20, 0.23, 0.95)
    // L'accento lo sceglie l'utente (Impostazioni > Personalizzazione > Colori).
    readonly property color accent: Config.accent
    readonly property color accentLight: light ? Qt.darker(Config.accent, 1.1) : Qt.lighter(Config.accent, 1.45)
    // Le icone del tema adatto alla modalità (chiare su scuro, scure su
    // chiaro): "image://icon/l/<nome>". Il prefisso fa anche ricaricare le
    // icone quando la modalità cambia. Sopra l'accento sempre bianche.
    readonly property string icons: "image://icon/" + Config.iconMode
    readonly property string iconsOnAccent: Config.iconMode === "l/" ? "image://icon/w/" : "image://icon/" + Config.iconMode

    // --- forme ---
    readonly property int radiusSmall: 6
    readonly property int radiusLarge: 10
    readonly property int radiusMenu: 8 // menu del tasto destro, come Windows 11
    readonly property int taskbarHeight: 48

    // --- tipografia ---
    readonly property int fontSmall: 12
    readonly property int fontNormal: 14

    // --- movimento ---
    readonly property int fast: 120
    readonly property int normal: 200
    readonly property int slow: 250 // pannelli e finestre che entrano in scena
    // cubic-bezier(0, 0, 0.2, 1): parte decisa, si posa morbida
    readonly property var decelerate: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0]
    // per le uscite: parte piano e se ne va veloce
    readonly property var accelerate: [0.7, 0.0, 0.84, 0.0, 1.0, 1.0]
}
