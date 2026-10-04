pragma Singleton

import QtQuick

// I colori e le misure delle Impostazioni: quelli di Windows 11 in tema
// scuro, con il fondo Mica della barra del titolo di Vela e l'accento
// scelto dall'utente.
QtObject {
    property bool windowActive: true // lo aggiorna Main.qml

    // --- fondo e superfici ---
    readonly property color background: windowActive ? Prefs.mica : Prefs.micaInactive
    readonly property color card: Qt.rgba(1, 1, 1, 0.051)
    readonly property color cardHover: Qt.rgba(1, 1, 1, 0.083)
    readonly property color cardPressed: Qt.rgba(1, 1, 1, 0.035)
    readonly property color cardStroke: Qt.rgba(0, 0, 0, 0.18)
    readonly property color divider: Qt.rgba(1, 1, 1, 0.084)
    readonly property color control: Qt.rgba(1, 1, 1, 0.061)
    readonly property color controlHover: Qt.rgba(1, 1, 1, 0.084)
    readonly property color controlPressed: Qt.rgba(1, 1, 1, 0.033)
    readonly property color controlStroke: Qt.rgba(1, 1, 1, 0.07)
    readonly property color controlStrokeStrong: Qt.rgba(1, 1, 1, 0.55)
    readonly property color flyout: "#2c2c2c"
    readonly property color dialog: "#2b2b2b"
    readonly property color smoke: Qt.rgba(0, 0, 0, 0.3) // dietro ai dialoghi
    readonly property color subtleHover: Qt.rgba(1, 1, 1, 0.061)
    readonly property color subtlePressed: Qt.rgba(1, 1, 1, 0.042)

    // --- testo ---
    readonly property color text: "#ffffff"
    readonly property color textSecondary: Qt.rgba(1, 1, 1, 0.786)
    readonly property color textTertiary: Qt.rgba(1, 1, 1, 0.545)
    readonly property color textDisabled: Qt.rgba(1, 1, 1, 0.363)
    readonly property color critical: "#ff99a4"
    readonly property color success: "#6ccb5f"

    // --- accento: in tema scuro Windows usa la sua tinta chiara, con testo nero ---
    readonly property color accent: Prefs.accent
    readonly property color accentFill: Qt.lighter(Prefs.accent, 1.35)
    readonly property color accentFillHover: Qt.lighter(Prefs.accent, 1.25)
    readonly property color accentText: "#000000"

    // --- forme e testo ---
    readonly property int radius: 4 // controlli
    readonly property int radiusCard: 6
    readonly property int radiusOverlay: 8
    readonly property int fontCaption: 12
    readonly property int fontBody: 14
    readonly property int fontSubtitle: 20
    readonly property int fontTitle: 28

    // --- movimento (come la shell) ---
    readonly property int fast: 120
    readonly property int normal: 200
    readonly property var decelerate: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0]
}
