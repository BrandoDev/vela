// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQuick

// Windows 11's (Fluent) colors and sizes, light or dark, with the accent the
// user chose. The same values as Settings (settings/qml/Theme.qml), which will
// move to this module.
QtObject {
    readonly property bool light: Style.light

    // --- surfaces ---
    readonly property color control: light ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.061)
    readonly property color controlHover: light ? Qt.rgba(0.976, 0.976, 0.976, 0.5) : Qt.rgba(1, 1, 1, 0.084)
    readonly property color controlPressed: light ? Qt.rgba(0.976, 0.976, 0.976, 0.3) : Qt.rgba(1, 1, 1, 0.033)
    readonly property color controlStroke: light ? Qt.rgba(0, 0, 0, 0.07) : Qt.rgba(1, 1, 1, 0.07)
    readonly property color controlStrokeStrong: light ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(1, 1, 1, 0.55)
    readonly property color dialog: light ? "#fbfbfb" : "#2b2b2b"
    readonly property color dialogFooter: light ? "#f3f3f3" : "#202020"
    readonly property color dialogStroke: light ? Qt.rgba(0, 0, 0, 0.12) : Qt.rgba(0, 0, 0, 0.45)
    readonly property color divider: light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.084)
    readonly property color subtleHover: light ? Qt.rgba(0, 0, 0, 0.037) : Qt.rgba(1, 1, 1, 0.061)
    readonly property color subtlePressed: light ? Qt.rgba(0, 0, 0, 0.024) : Qt.rgba(1, 1, 1, 0.042)
    // The veil over the screen behind a system dialog (UAC): dark in light
    // mode too, like Windows.
    readonly property color veil: Effects.blurAvailable ? Qt.rgba(0, 0, 0, light ? 0.35 : 0.45) : Qt.rgba(0, 0, 0, 0.6)

    // --- text ---
    readonly property color text: light ? Qt.rgba(0, 0, 0, 0.894) : "#ffffff"
    readonly property color textSecondary: light ? Qt.rgba(0, 0, 0, 0.62) : Qt.rgba(1, 1, 1, 0.786)
    readonly property color textTertiary: light ? Qt.rgba(0, 0, 0, 0.447) : Qt.rgba(1, 1, 1, 0.545)
    readonly property color critical: light ? "#c42b1c" : "#ff99a4"
    readonly property color caution: light ? "#9d5d00" : "#fce100"

    // --- accent: in dark mode the light tint with black text, in light mode
    // the dark one with white text ---
    readonly property color accent: Style.accent
    readonly property color accentFill: light ? Qt.darker(Style.accent, 1.15) : Qt.lighter(Style.accent, 1.35)
    readonly property color accentFillHover: light ? Qt.darker(Style.accent, 1.05) : Qt.lighter(Style.accent, 1.25)
    readonly property color accentText: light ? "#ffffff" : "#000000"
    readonly property color link: light ? Qt.darker(Style.accent, 1.3) : Qt.lighter(Style.accent, 1.5)
    // Icons of the theme matching the mode ("image://icon/l/<name>").
    readonly property string icons: "image://icon/" + Style.iconMode

    // --- shapes and type ---
    readonly property int radius: 4 // controls
    readonly property int radiusOverlay: 8 // dialogs
    readonly property int fontCaption: 12
    readonly property int fontBody: 14
    readonly property int fontBodyLarge: 18
    readonly property int fontSubtitle: 20

    // --- motion (like the shell and the compositor) ---
    readonly property int fast: 120
    readonly property int normal: 200
    readonly property int slow: 250
    readonly property var decelerate: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0]
    readonly property var accelerate: [0.7, 0.0, 0.84, 0.0, 1.0, 1.0]
}
