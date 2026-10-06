// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Dialogs
import QtCore

// Personalizzazione > Sfondo: le immagini recenti, "Sfoglia foto" e gli
// sfondi installati nel sistema.
Page {
    id: page

    function urlFor(path) {
        return path.startsWith(":") ? "qrc" + path : "file://" + path
    }

    PersonalizationPage.DesktopPreview {}

    component Thumb: Rectangle {
        id: thumb
        required property string path
        width: 120
        height: 80
        radius: Theme.radius
        color: "#101010"
        border.width: Prefs.wallpaper === path ? 2 : 0
        border.color: Theme.accentFill
        Image {
            anchors { fill: parent; margins: Prefs.wallpaper === thumb.path ? 3 : 0 }
            source: page.urlFor(thumb.path)
            sourceSize: Qt.size(240, 160)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "white"
            opacity: thumbMouse.containsMouse ? 0.08 : 0
        }
        MouseArea {
            id: thumbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Prefs.setWallpaper(thumb.path)
        }
    }

    CardGroup {
        Card {
            icon: "preferences-desktop-wallpaper"
            title: qsTr("Personalize your background")
            description: qsTr("One picture fills every screen")
            trailing: Choice { model: [qsTr("Picture")]; currentIndex: 0 }
            contentItem: Column {
                width: parent.width
                spacing: 8
                Text {
                    text: qsTr("Recent images")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    leftPadding: 36
                }
                Flow {
                    x: 36
                    width: parent.width - 36
                    spacing: 8
                    Repeater {
                        model: [":/vela/images/vela_splash_169.svg"].concat(Prefs.recentWallpapers)
                        delegate: Thumb { required property string modelData; path: modelData }
                    }
                }
            }
        }
        Card {
            icon: "folder-pictures"
            title: qsTr("Choose a photo")
            trailing: Button {
                text: qsTr("Browse photos")
                onClicked: fileDialog.open()
            }
        }
    }

    CardGroup {
        title: qsTr("System backgrounds")
        visible: system.count > 0
        Card {
            minimumHeight: 0
            contentItem: Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    id: system
                    model: Prefs.systemWallpapers
                    delegate: Thumb { required property string modelData; path: modelData }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Choose a photo")
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        nameFilters: [qsTr("Images (*.jpg *.jpeg *.png *.webp *.bmp *.gif *.svg *.avif *.jxl)")]
        onAccepted: Prefs.setWallpaper(selectedFile.toString())
    }
}
