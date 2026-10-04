pragma Singleton

import QtQuick

// I colori e le misure di Esplora: quelli di Windows 11 in tema scuro, con
// il fondo Mica della barra del titolo di Vela e l'accento della shell. I
// nomi in comune con il Theme della shell servono a MenuPanel.qml, che è
// lo stesso componente dei menu del desktop.
QtObject {
    property bool windowActive: true // lo aggiorna Main.qml

    // --- fondo e superfici ---
    readonly property color background: windowActive ? Look.mica : Look.micaInactive
    readonly property color layer: Qt.rgba(0.227, 0.227, 0.227, 0.30) // l'area dei file e della navigazione
    readonly property color layerStroke: Qt.rgba(0, 0, 0, 0.10)
    readonly property color control: Qt.rgba(1, 1, 1, 0.061)
    readonly property color controlHover: Qt.rgba(1, 1, 1, 0.084)
    readonly property color controlStroke: Qt.rgba(1, 1, 1, 0.07)
    readonly property color selection: Qt.rgba(1, 1, 1, 0.10)
    readonly property color selectionHover: Qt.rgba(1, 1, 1, 0.13)
    readonly property color divider: Qt.rgba(1, 1, 1, 0.084)
    readonly property color dialog: "#2b2b2b"
    readonly property color critical: "#ff99a4"

    // --- come il Theme della shell (MenuPanel) ---
    readonly property color popup: "#2c2c2c"
    readonly property color stroke: Qt.rgba(1, 1, 1, 0.09)
    readonly property color hover: Qt.rgba(1, 1, 1, 0.061)
    readonly property color pressed: Qt.rgba(1, 1, 1, 0.042)
    readonly property color text: "#ffffff"
    readonly property color textDim: Qt.rgba(1, 1, 1, 0.62)
    readonly property color accent: Look.accent
    readonly property color accentLight: Qt.lighter(Look.accent, 1.35)
    readonly property color accentText: "#000000"
    readonly property int radiusSmall: 4
    readonly property int radiusMenu: 8
    readonly property int radiusLarge: 8
    readonly property int fontSmall: 12
    readonly property int fontNormal: 14
    readonly property int fast: 120
    readonly property int normal: 200
    readonly property int slow: 250
    readonly property var decelerate: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0]
}
