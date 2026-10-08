// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// The volume indicator, like Windows 11's: at the bottom in the middle, above
// the taskbar, for a moment after a volume key, the knob or the wheel on the
// taskbar's volume icon. The wheel on an app's taskbar button shows that app's
// volume instead, with its icon. It can be used too: dragged, the wheel, a
// click on the icon mutes. It stays while the mouse is on it.
Window {
    id: root
    objectName: "volumeOsd"
    visible: false
    width: 232
    height: 64 // the pill and the room it rises from
    color: "transparent"

    // The app shown (Mixer.apps' key), or "" for the system volume.
    property string appKey: ""
    readonly property var app: appKey !== "" ? Mixer.apps.find(a => a.key === appKey) || null : null
    readonly property real volume: app ? app.volume : Status.volume
    readonly property bool muted: app ? app.muted : Status.muted

    Connections {
        target: Status
        function onVolumeOsdRequested() {
            // Quick settings already shows the volume.
            if (!Menus.quickSettingsOpen) {
                root.appKey = ""
                root.show()
            }
        }
    }
    Connections {
        target: Mixer
        function onAppOsdRequested(key) {
            root.appKey = key
            root.show()
        }
    }
    function setVolume(value) {
        if (app) {
            if (app.muted) Mixer.setAppMuted(app.key, false)
            Mixer.setAppVolume(app.key, value)
        } else {
            if (Status.muted) Status.setMuted(false)
            Status.setVolume(value)
        }
    }
    function step(direction) {
        if (app) {
            Mixer.stepAppVolume(app.key, direction)
        } else {
            Status.stepVolume(direction)
        }
    }
    function toggleMute() {
        if (app) {
            Mixer.setAppMuted(app.key, !app.muted)
        } else {
            Status.setMuted(!Status.muted)
        }
    }

    function show() {
        if (!visible) {
            Menus.placeOnTargetScreen(root)
            visible = true
            Effects.setBlur(root, [Qt.rect(pill.x, 12, pill.width, pill.height)])
            pill.opacity = 0
            slide.y = 12
            fade.stop()
            appear.restart()
        } else if (fade.running) {
            fade.stop()
            appear.restart()
        }
        hideTimer.restart()
    }

    ParallelAnimation {
        id: appear
        NumberAnimation { target: pill; property: "opacity"; to: 1; duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        NumberAnimation { target: slide; property: "y"; to: 0; duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
    }
    SequentialAnimation {
        id: fade
        NumberAnimation { target: pill; property: "opacity"; to: 0; duration: Theme.normal }
        ScriptAction { script: root.visible = false }
    }
    Timer {
        id: hideTimer
        interval: 2000
        onTriggered: {
            if (hover.hovered || barMouse.pressed) {
                restart()
            } else {
                fade.start()
            }
        }
    }

    // The wheel: a step every notch (120), touchpads add up their small ones.
    property real wheelRest: 0
    function wheel(delta) {
        wheelRest += delta
        while (Math.abs(wheelRest) >= 120) {
            step(wheelRest > 0 ? 1 : -1)
            wheelRest -= wheelRest > 0 ? 120 : -120
        }
        hideTimer.restart()
    }

    Rectangle {
        id: pill
        x: 0
        y: 12
        width: parent.width
        height: 52
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
        transform: Translate { id: slide }

        HoverHandler { id: hover }
        MouseArea {
            anchors.fill: parent
            onWheel: wheel => root.wheel(wheel.angleDelta.y)
        }

        // The speaker (or the app's icon): a click mutes, like the one in
        // quick settings.
        Item {
            id: speaker
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 36
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: Theme.hover
                opacity: speakerMouse.containsMouse ? 1 : 0
            }
            Image {
                anchors.centerIn: parent
                width: 18
                height: 18
                opacity: root.app && root.app.muted ? 0.45 : 1
                source: Theme.icons + encodeURIComponent(root.app ? root.app.icon : Status.volumeIconName)
                sourceSize: Qt.size(width, height)
            }
            Image {
                visible: !!root.app && root.app.muted
                anchors { right: parent.right; bottom: parent.bottom; margins: 5 }
                width: 12
                height: 12
                source: Theme.icons + "audio-volume-muted"
                sourceSize: Qt.size(width, height)
            }
            MouseArea {
                id: speakerMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    root.toggleMute()
                    hideTimer.restart()
                }
            }
        }

        Item {
            id: bar
            readonly property real shown: Math.max(0, Math.min(1, root.volume))
            anchors { left: speaker.right; leftMargin: 10; right: number.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            height: 20
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4
                radius: 2
                color: Theme.stroke
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * bar.shown
                height: 4
                radius: 2
                color: root.muted ? Theme.textDim : Theme.accent
            }
            Rectangle {
                x: parent.width * bar.shown - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: Theme.popup
                border.width: 1
                border.color: Theme.stroke
                Rectangle {
                    anchors.centerIn: parent
                    width: barMouse.pressed ? 8 : 10
                    height: width
                    radius: width / 2
                    color: root.muted ? Theme.textDim : Theme.accent
                }
            }
            MouseArea {
                id: barMouse
                anchors { fill: parent; margins: -8 }
                function update(x) {
                    root.setVolume(Math.max(0, Math.min(1, (x - 8) / bar.width)))
                    hideTimer.restart()
                }
                onPressed: mouse => update(mouse.x)
                onPositionChanged: mouse => { if (pressed) update(mouse.x) }
                onWheel: wheel => root.wheel(wheel.angleDelta.y)
            }
        }

        Text {
            id: number
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            width: 28 // "100" without the bar moving
            horizontalAlignment: Text.AlignRight
            text: Math.round(root.volume * 100)
            color: root.muted ? Theme.textDim : Theme.text
            font.pixelSize: Theme.fontNormal
            font.features: { "tnum": 1 }
        }
    }
}
