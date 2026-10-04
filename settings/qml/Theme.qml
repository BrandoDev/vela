pragma Singleton

import QtQuick

// I colori e le misure delle Impostazioni: quelli di Windows 11, in tema
// scuro o chiaro secondo la modalità delle app, con il fondo Mica della
// barra del titolo di Vela e l'accento scelto dall'utente.
QtObject {
    property bool windowActive: true // lo aggiorna Main.qml
    readonly property bool light: Prefs.light

    // --- fondo e superfici ---
    readonly property color background: windowActive ? Prefs.mica : Prefs.micaInactive
    readonly property color card: light ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.051)
    readonly property color cardHover: light ? Qt.rgba(0.976, 0.976, 0.976, 0.5) : Qt.rgba(1, 1, 1, 0.083)
    readonly property color cardPressed: light ? Qt.rgba(0.976, 0.976, 0.976, 0.3) : Qt.rgba(1, 1, 1, 0.035)
    readonly property color cardStroke: light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(0, 0, 0, 0.18)
    readonly property color divider: light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.084)
    readonly property color control: light ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.061)
    readonly property color controlHover: light ? Qt.rgba(0.976, 0.976, 0.976, 0.5) : Qt.rgba(1, 1, 1, 0.084)
    readonly property color controlPressed: light ? Qt.rgba(0.976, 0.976, 0.976, 0.3) : Qt.rgba(1, 1, 1, 0.033)
    readonly property color controlStroke: light ? Qt.rgba(0, 0, 0, 0.07) : Qt.rgba(1, 1, 1, 0.07)
    readonly property color controlStrokeStrong: light ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(1, 1, 1, 0.55)
    readonly property color flyout: light ? "#f9f9f9" : "#2c2c2c"
    readonly property color dialog: light ? "#fbfbfb" : "#2b2b2b"
    readonly property color smoke: Qt.rgba(0, 0, 0, 0.3) // dietro ai dialoghi
    readonly property color subtleHover: light ? Qt.rgba(0, 0, 0, 0.037) : Qt.rgba(1, 1, 1, 0.061)
    readonly property color subtlePressed: light ? Qt.rgba(0, 0, 0, 0.024) : Qt.rgba(1, 1, 1, 0.042)

    // --- testo ---
    readonly property color text: light ? Qt.rgba(0, 0, 0, 0.894) : "#ffffff"
    readonly property color textSecondary: light ? Qt.rgba(0, 0, 0, 0.62) : Qt.rgba(1, 1, 1, 0.786)
    readonly property color textTertiary: light ? Qt.rgba(0, 0, 0, 0.447) : Qt.rgba(1, 1, 1, 0.545)
    readonly property color textDisabled: light ? Qt.rgba(0, 0, 0, 0.361) : Qt.rgba(1, 1, 1, 0.363)
    readonly property color critical: light ? "#c42b1c" : "#ff99a4"
    readonly property color success: light ? "#0f7b0f" : "#6ccb5f"

    // --- accento: in tema scuro Windows usa la sua tinta chiara con testo
    // nero, in chiaro quella scura con testo bianco ---
    readonly property color accent: Prefs.accent
    readonly property color accentFill: light ? Qt.darker(Prefs.accent, 1.15) : Qt.lighter(Prefs.accent, 1.35)
    readonly property color accentFillHover: light ? Qt.darker(Prefs.accent, 1.05) : Qt.lighter(Prefs.accent, 1.25)
    readonly property color accentText: light ? "#ffffff" : "#000000"
    // Le icone del tema adatto alla modalità ("image://icon/l/<nome>"; il
    // prefisso le fa ricaricare quando la modalità cambia).
    readonly property string icons: "image://icon/" + Prefs.iconMode

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
