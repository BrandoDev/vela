// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

pragma Singleton

import QtQuick

// Vela's visual language in one place. Curves and durations are the same as
// the compositor's (compositor/src/motion.h): windows and shell move the same
// way.
QtObject {
    // --- colors: dark or light theme ("Choose your mode" in Settings) ---
    readonly property bool light: Config.shellTheme === "light"
    readonly property color desktop: "#06182d" // below the wallpaper, its darkest blue
    // With the compositor's blur (docs/renderer.md §8.3) panels are acrylic
    // like on Windows 11: semi-transparent over the blurred background.
    // Without it, opaque.
    readonly property bool acrylic: Effects.blurAvailable
    readonly property color taskbar: light
        ? (acrylic ? Qt.rgba(0.95, 0.95, 0.96, 0.66) : Qt.rgba(0.94, 0.94, 0.95, 0.96))
        : (acrylic ? Qt.rgba(0.11, 0.11, 0.13, 0.62) : Qt.rgba(0.10, 0.10, 0.12, 0.94))
    readonly property color surface: light
        ? (acrylic ? Qt.rgba(0.96, 0.96, 0.97, 0.74) : Qt.rgba(0.95, 0.95, 0.96, 0.98))
        : (acrylic ? Qt.rgba(0.14, 0.14, 0.16, 0.72) : Qt.rgba(0.14, 0.14, 0.16, 0.97))
    // Dialogs (Run, Properties, confirmations): always solid.
    readonly property color dialog: light ? Qt.rgba(0.976, 0.976, 0.98, 0.99) : Qt.rgba(0.14, 0.14, 0.16, 0.98)
    readonly property color surfaceRaised: light ? Qt.rgba(1, 1, 1, 0.70) : Qt.rgba(1, 1, 1, 0.06)
    readonly property color popup: light
        ? (acrylic ? Qt.rgba(0.98, 0.98, 0.98, 0.82) : "#f9f9f9")
        : (acrylic ? Qt.rgba(0.17, 0.17, 0.19, 0.78) : "#2a2a2f") // menus
    readonly property color hover: light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.08)
    readonly property color pressed: light ? Qt.rgba(0, 0, 0, 0.03) : Qt.rgba(1, 1, 1, 0.05)
    readonly property color stroke: light ? Qt.rgba(0, 0, 0, 0.09) : Qt.rgba(1, 1, 1, 0.09)
    readonly property color text: light ? "#1b1b1b" : "#f3f3f6"
    readonly property color textDim: light ? "#5d5d66" : "#a4a4ae"
    // To lighten (or darken, in light theme) what's below: hover on colored
    // tiles, zone backgrounds in a panel.
    readonly property color overlay: light ? "black" : "white"
    readonly property color footer: light ? Qt.rgba(0, 0, 0, 0.035) : Qt.rgba(0, 0, 0, 0.18)
    // Dark panels over the output (Task View, Snap Assist).
    readonly property color backdrop: light
        ? (acrylic ? Qt.rgba(0.93, 0.93, 0.95, 0.55) : Qt.rgba(0.92, 0.92, 0.94, 0.96))
        : (acrylic ? Qt.rgba(0.05, 0.05, 0.07, 0.55) : Qt.rgba(0.07, 0.07, 0.09, 0.96))
    readonly property color card: light ? Qt.rgba(0.98, 0.98, 0.99, 0.92) : Qt.rgba(0.15, 0.15, 0.17, 0.92)
    readonly property color cardHover: light ? Qt.rgba(1, 1, 1, 0.97) : Qt.rgba(0.20, 0.20, 0.23, 0.95)
    // The user chooses the accent (Settings > Personalization > Colors).
    readonly property color accent: Config.accent
    readonly property color accentLight: light ? Qt.darker(Config.accent, 1.1) : Qt.lighter(Config.accent, 1.45)
    // Icons from the theme matching the mode (light on dark, dark on light):
    // "image://icon/l/<name>". The prefix also reloads icons when the mode
    // changes. Always white on the accent.
    readonly property string icons: "image://icon/" + Config.iconMode
    readonly property string iconsOnAccent: Config.iconMode === "l/" ? "image://icon/w/" : "image://icon/" + Config.iconMode

    // --- shapes ---
    readonly property int radiusSmall: 6
    readonly property int radiusLarge: 10
    readonly property int radiusMenu: 8 // right-click menus, like Windows 11
    readonly property int taskbarHeight: 48

    // --- typography ---
    readonly property int fontSmall: 12
    readonly property int fontNormal: 14

    // --- motion ---
    readonly property int fast: 120
    readonly property int normal: 200
    readonly property int slow: 250 // panels and windows entering the scene
    // cubic-bezier(0, 0, 0.2, 1): a decisive start, a soft landing
    readonly property var decelerate: [0.0, 0.0, 0.2, 1.0, 1.0, 1.0]
    // for exits: starts slowly and leaves fast
    readonly property var accelerate: [0.7, 0.0, 0.84, 0.0, 1.0, 1.0]
}
