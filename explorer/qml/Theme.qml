// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQuick

// Explorer's colors and sizes: Windows 11's, in dark or light theme according
// to the apps' mode, with the Mica background of Vela's title bar and the
// shell's accent. The names shared with the shell's Theme are for
// MenuPanel.qml, the same component as the desktop menus.
QtObject {
    property bool windowActive: true // Main.qml updates it
    readonly property bool light: Look.light

    // --- background and surfaces ---
    readonly property color background: windowActive ? Look.mica : Look.micaInactive
    readonly property color layer: light ? Qt.rgba(1, 1, 1, 0.5) : Qt.rgba(0.227, 0.227, 0.227, 0.30) // the files and navigation area
    readonly property color layerStroke: light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(0, 0, 0, 0.10)
    readonly property color control: light ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.061)
    readonly property color controlHover: light ? Qt.rgba(0.976, 0.976, 0.976, 0.5) : Qt.rgba(1, 1, 1, 0.084)
    readonly property color controlStroke: light ? Qt.rgba(0, 0, 0, 0.07) : Qt.rgba(1, 1, 1, 0.07)
    readonly property color selection: light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10)
    readonly property color selectionHover: light ? Qt.rgba(0, 0, 0, 0.09) : Qt.rgba(1, 1, 1, 0.13)
    readonly property color divider: light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.084)
    readonly property color dialog: light ? "#fbfbfb" : "#2b2b2b"
    readonly property color critical: light ? "#c42b1c" : "#ff99a4"
    readonly property color field: light ? "#ffffff" : "#1e1e1e" // a text field while typing
    readonly property color focusRing: light ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(1, 1, 1, 0.35)
    readonly property color dialogFooter: light ? "#f3f3f3" : "#202020"
    readonly property color scrollTrack: light ? Qt.rgba(0, 0, 0, 0.04) : Qt.rgba(1, 1, 1, 0.05)
    readonly property color scrollThumb: light ? Qt.rgba(0, 0, 0, 0.35) : Qt.rgba(1, 1, 1, 0.35)
    readonly property color scrollThumbHover: light ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(1, 1, 1, 0.55)

    // --- like the shell's Theme (MenuPanel) ---
    readonly property color popup: light ? "#f9f9f9" : "#2c2c2c"
    readonly property color stroke: light ? Qt.rgba(0, 0, 0, 0.09) : Qt.rgba(1, 1, 1, 0.09)
    readonly property color hover: light ? Qt.rgba(0, 0, 0, 0.037) : Qt.rgba(1, 1, 1, 0.061)
    readonly property color pressed: light ? Qt.rgba(0, 0, 0, 0.024) : Qt.rgba(1, 1, 1, 0.042)
    readonly property color text: light ? Qt.rgba(0, 0, 0, 0.894) : "#ffffff"
    readonly property color textDim: light ? Qt.rgba(0, 0, 0, 0.62) : Qt.rgba(1, 1, 1, 0.62)
    // In dark theme Windows fills with the light accent and black text, in
    // light theme with the dark accent and white text.
    readonly property color accent: Look.accent
    readonly property color accentLight: light ? Qt.darker(Look.accent, 1.15) : Qt.lighter(Look.accent, 1.35)
    readonly property color accentText: light ? "#ffffff" : "#000000"
    // Icons from the theme matching the mode ("image://icon/l/<name>"; the
    // prefix reloads the icons when the mode changes).
    readonly property string icons: "image://icon/" + Look.iconMode
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
