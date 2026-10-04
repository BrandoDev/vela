import QtQuick

// Personalizzazione > Colori: la modalità delle app (chiara o scura) e il
// colore principale, con la tavolozza di Windows 11.
Page {
    id: page
    readonly property var accents: [
        "#5b8cff", "#ffb900", "#ff8c00", "#f7630c", "#ca5010", "#da3b01", "#ef6950", "#d13438",
        "#ff4343", "#e74856", "#e81123", "#ea005e", "#c30052", "#e3008c", "#bf0077", "#c239b3",
        "#9a0089", "#0078d4", "#0063b1", "#8e8cd8", "#6b69d6", "#8764b8", "#744da9", "#b146c2",
        "#881798", "#0099bc", "#2d7d9a", "#00b7c3", "#038387", "#00b294", "#018574", "#00cc6a",
        "#10893e", "#7a7574", "#5d5a58", "#68768a", "#515c6b", "#567c73", "#486860", "#498205",
        "#107c10", "#767676", "#4c4a48", "#69797e", "#4a5459", "#647c64", "#525e54", "#847545"
    ]

    PersonalizationPage.DesktopPreview {}

    CardGroup {
        Card {
            icon: "preferences-desktop-theme"
            title: "Scegli la modalità"
            description: "Il colore delle finestre delle app. Barra delle applicazioni e menu restano scuri."
            trailing: Choice {
                model: ["Scuro", "Chiaro"]
                currentIndex: Prefs.appTheme === "light" ? 1 : 0
                onChosen: index => Prefs.appTheme = index === 1 ? "light" : "dark"
            }
        }
        Card {
            icon: "preferences-desktop-color"
            title: "Colore principale"
            description: "Pulsanti, selezioni, elementi attivi della barra delle applicazioni e dei menu"
            trailing: Rectangle {
                width: 32
                height: 32
                radius: Theme.radius
                color: Prefs.accent
                border.width: 1
                border.color: Theme.controlStroke
            }
            contentItem: Column {
                width: parent.width
                spacing: 8
                Text {
                    text: "Colori"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    leftPadding: 36
                }
                Grid {
                    x: 36
                    columns: Math.max(1, Math.floor((parent.width - 36) / 48))
                    spacing: 4
                    Repeater {
                        model: page.accents
                        delegate: Rectangle {
                            id: swatch
                            required property string modelData
                            readonly property bool chosen: Prefs.accent.toString().toLowerCase() === modelData
                            width: 44
                            height: 44
                            radius: Theme.radius
                            color: "transparent"
                            border.width: chosen ? 2 : swatchMouse.containsMouse ? 1 : 0
                            border.color: chosen ? Theme.text : Theme.controlStrokeStrong
                            Rectangle {
                                anchors { fill: parent; margins: 4 }
                                radius: 2
                                color: swatch.modelData
                                Text {
                                    visible: swatch.chosen
                                    anchors { right: parent.right; top: parent.top; margins: 2 }
                                    text: "✓"
                                    color: "white"
                                    font.pixelSize: 12
                                    font.weight: Font.Bold
                                }
                            }
                            MouseArea {
                                id: swatchMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Prefs.accent = swatch.modelData
                            }
                        }
                    }
                }
            }
        }
    }
}
