pragma Singleton

import QtQuick

// Linguaggio visivo di Vela in un solo posto. Le curve e le durate sono le
// stesse del compositor (compositor/src/motion.hpp): finestre e shell si
// muovono nello stesso modo.
QtObject {
    // --- colori (tema scuro) ---
    readonly property color desktop: "#06182d" // sotto lo sfondo, il suo blu più scuro
    // Con la sfocatura del compositor (docs/renderer.md §8.3) i pannelli sono
    // acrylic come su Windows 11: semitrasparenti sopra lo sfondo sfocato.
    // Senza, opachi.
    readonly property bool acrylic: Effects.blurAvailable
    readonly property color taskbar: acrylic ? Qt.rgba(0.11, 0.11, 0.13, 0.62) : Qt.rgba(0.10, 0.10, 0.12, 0.94)
    readonly property color surface: acrylic ? Qt.rgba(0.14, 0.14, 0.16, 0.72) : Qt.rgba(0.14, 0.14, 0.16, 0.97)
    // Finestre di dialogo (Esegui, Proprietà, conferme): sempre piene.
    readonly property color dialog: Qt.rgba(0.14, 0.14, 0.16, 0.98)
    readonly property color surfaceRaised: Qt.rgba(1, 1, 1, 0.06)
    readonly property color popup: acrylic ? Qt.rgba(0.17, 0.17, 0.19, 0.78) : "#2a2a2f" // menu
    readonly property color hover: Qt.rgba(1, 1, 1, 0.08)
    readonly property color pressed: Qt.rgba(1, 1, 1, 0.05)
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.09)
    readonly property color text: "#f3f3f6"
    readonly property color textDim: "#a4a4ae"
    readonly property color accent: "#5b8cff"
    readonly property color accentLight: "#9ab8ff"

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
