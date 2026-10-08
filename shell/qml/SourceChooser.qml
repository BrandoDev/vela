// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// What to share, when an app (video call, recording) asks for the screen
// through the portal: a whole output or a window, with previews. The answer
// goes back to xdg-desktop-portal-wlr (see "vela-shell --choose-source" and
// session/xdpw-vela.conf.in).
Window {
    id: root
    objectName: "sourceChooser"
    visible: false
    width: 760
    height: 540
    color: "transparent"

    property int tab: 0 // 0 outputs, 1 windows
    property string selected: "" // the answer for the portal
    readonly property var current: tab === 0 ? screens : windows

    ListModel { id: screens }
    ListModel { id: windows }

    Connections {
        target: Shell
        function onChooseSourceRequested() {
            screens.clear()
            for (const screen of Qt.application.screens) {
                if (screen.name !== "") {
                    screens.append({
                        identifier: "screen:" + screen.name,
                        answer: "Monitor: " + screen.name,
                        // The monitor's model: the resolution isn't known here
                        // (at fractional scales devicePixelRatio is the
                        // integer one).
                        title: screen.model !== "" ? screen.name + " (" + screen.model + ")" : screen.name,
                        icon: "video-display",
                        revision: 0
                    })
                }
            }
            windows.clear()
            const ids = []
            for (const w of Capture.windowList()) {
                windows.append({
                    identifier: w.id,
                    answer: "Window: " + w.id,
                    title: w.title !== "" ? w.title : w.appId,
                    icon: Apps.iconForAppId(w.appId),
                    revision: 0
                })
                ids.push(w.id)
            }
            // First the photos of the outputs, then the window: otherwise it
            // would be in the previews itself.
            Capture.captureScreens()
            Capture.capture(ids)
            root.tab = 0
            root.selected = screens.count === 1 ? screens.get(0).answer : ""
            showTimer.restart()
        }
        function onChooseSourceCancelled() {
            showTimer.stop()
            root.visible = false
        }
    }

    Timer {
        id: showTimer
        interval: 300 // at most: if a capture doesn't arrive, it opens anyway
        onTriggered: {
            root.visible = true
            panel.forceActiveFocus()
        }
    }

    Connections {
        target: Capture
        function onThumbnailReady(identifier) {
            for (const model of [screens, windows]) {
                for (let i = 0; i < model.count; ++i) {
                    if (model.get(i).identifier === identifier) {
                        model.setProperty(i, "revision", model.get(i).revision + 1)
                    }
                }
            }
            // All outputs photographed: it can open.
            if (showTimer.running) {
                let ready = true
                for (let i = 0; i < screens.count; ++i) {
                    ready = ready && screens.get(i).revision > 0
                }
                if (ready) {
                    showTimer.stop()
                    showTimer.triggered()
                }
            }
        }
    }

    function answer(text) {
        visible = false
        Shell.chooseSource(text)
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke
        focus: true
        Keys.onEscapePressed: root.answer("")
        Keys.onReturnPressed: if (root.selected !== "") root.answer(root.selected)
        Keys.onEnterPressed: if (root.selected !== "") root.answer(root.selected)
        Keys.onTabPressed: root.tab = 1 - root.tab

        Text {
            x: 24
            y: 20
            text: qsTr("Choose what to share")
            color: Theme.text
            font.pixelSize: Theme.fontNormal + 4
            font.weight: Font.DemiBold
        }
        Text {
            x: 24
            y: 50
            text: qsTr("An app wants to see your screen. It will only see what you choose here.")
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
        }

        // The two tabs, underlined like in Windows 11.
        Row {
            x: 24
            y: 84
            spacing: 4
            Repeater {
                model: [qsTr("Full screen"), qsTr("Window")]
                delegate: Item {
                    required property string modelData
                    required property int index
                    width: label.implicitWidth + 24
                    height: 36
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: Theme.hover
                        opacity: tabMouse.containsMouse ? 1 : 0
                    }
                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: root.tab === parent.index ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.weight: root.tab === parent.index ? Font.DemiBold : Font.Normal
                    }
                    Rectangle {
                        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                        width: 20
                        height: 3
                        radius: 1.5
                        color: Theme.accent
                        visible: root.tab === parent.index
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.tab = parent.index
                    }
                }
            }
        }

        GridView {
            id: grid
            x: 16
            y: 132
            width: parent.width - 32
            height: parent.height - y - 76
            clip: true
            model: root.current
            cellWidth: Math.floor(width / 3)
            cellHeight: 186
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: card
                required property string identifier
                required property string answer
                required property string title
                required property string icon
                required property int revision
                readonly property bool chosen: root.selected === answer
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors { fill: parent; margins: 6 }
                    radius: Theme.radiusSmall + 2
                    color: card.chosen ? Qt.rgba(0.36, 0.55, 1.0, 0.18) : cardMouse.containsMouse ? Theme.hover : "transparent"
                    border.width: card.chosen ? 2 : 0
                    border.color: Theme.accent
                }

                Rectangle {
                    id: frame
                    x: 16
                    y: 14
                    width: parent.width - 32
                    height: 124
                    radius: 4
                    color: "#101014"
                    clip: true
                    Image {
                        id: thumbnail
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        source: card.revision > 0 ? "image://thumbnail/" + card.identifier + "/" + card.revision : ""
                        smooth: true
                        mipmap: true
                    }
                    Image {
                        anchors.centerIn: parent
                        visible: thumbnail.status !== Image.Ready
                        width: 48
                        height: 48
                        source: Theme.icons + encodeURIComponent(card.icon)
                        sourceSize: Qt.size(width, height)
                    }
                }
                Row {
                    x: 16
                    anchors { top: frame.bottom; topMargin: 10 }
                    width: parent.width - 32
                    spacing: 8
                    Image {
                        width: 16
                        height: 16
                        source: Theme.icons + encodeURIComponent(card.icon)
                        sourceSize: Qt.size(width, height)
                    }
                    Text {
                        width: parent.width - 24
                        text: card.title
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.selected = card.answer
                    onDoubleClicked: root.answer(card.answer)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: root.tab === 0 ? qsTr("No screens") : qsTr("No open windows")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
        }

        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 20 }
            spacing: 8

            component DialogButton: Rectangle {
                id: button
                property string label
                property bool primary: false
                property bool usable: true
                signal clicked()
                width: 110
                height: 32
                radius: Theme.radiusSmall
                opacity: usable ? 1 : 0.5
                color: primary ? (mouse.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent)
                               : (mouse.pressed ? Theme.pressed : mouse.containsMouse ? Theme.hover : Theme.surfaceRaised)
                border.width: primary ? 0 : 1
                border.color: Theme.stroke
                Text {
                    anchors.centerIn: parent
                    text: button.label
                    color: button.primary ? "white" : Theme.text
                    font.pixelSize: Theme.fontNormal
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: button.usable
                    onClicked: button.clicked()
                }
            }

            DialogButton {
                label: qsTr("Share")
                primary: true
                usable: root.selected !== ""
                onClicked: root.answer(root.selected)
            }
            DialogButton {
                label: qsTr("Cancel")
                onClicked: root.answer("")
            }
        }
    }
}
