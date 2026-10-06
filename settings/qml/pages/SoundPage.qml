// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Sistema > Audio: dove va il suono e da dove arriva, con il volume di
// ciascuno, come Windows 11.
Page {
    id: page
    Component.onCompleted: Audio.refresh()

    readonly property var defaultOutput: Audio.outputs.find(d => d.isDefault)
    readonly property var defaultInput: Audio.inputs.find(d => d.isDefault)

    component DeviceCard: Card {
        id: device
        required property var modelData
        required property string kind
        icon: kind === "output" ? (modelData.description.toLowerCase().indexOf("hdmi") >= 0 || modelData.description.toLowerCase().indexOf("displayport") >= 0 ? "video-display" : "audio-speakers") : "audio-input-microphone"
        title: modelData.description
        description: modelData.isDefault ? "Dispositivo predefinito" : ""
        clickable: !modelData.isDefault
        onClicked: Audio.setDefault(kind, modelData.name)
        trailing: Rectangle {
            // Il pallino di scelta, come i RadioButton di Windows.
            width: 20
            height: 20
            radius: 10
            color: device.modelData.isDefault ? Theme.accentFill : "transparent"
            border.width: device.modelData.isDefault ? 0 : 1
            border.color: Theme.controlStrokeStrong
            Rectangle {
                visible: device.modelData.isDefault
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 5
                color: Theme.accentText
            }
        }
    }

    component VolumeCard: Card {
        id: volumeCard
        required property var device
        required property string kind
        icon: device && device.muted ? "audio-volume-muted" : kind === "output" ? "audio-volume-high" : "audio-input-microphone"
        title: "Volume"
        trailing: Row {
            spacing: 8
            Button {
                subtle: true
                icon: volumeCard.device && volumeCard.device.muted ? (volumeCard.kind === "output" ? "audio-volume-muted" : "microphone-sensitivity-muted") : (volumeCard.kind === "output" ? "audio-volume-high" : "audio-input-microphone")
                onClicked: Audio.setMuted(volumeCard.kind, volumeCard.device.name, !volumeCard.device.muted)
            }
            Slider {
                anchors.verticalCenter: parent.verticalCenter
                width: 220
                to: 1
                value: volumeCard.device ? Math.min(1, volumeCard.device.volume) : 0
                onMoved: value => Audio.setVolume(volumeCard.kind, volumeCard.device.name, value)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                text: volumeCard.device ? Math.round(volumeCard.device.volume * 100) : ""
                color: Theme.text
                font.pixelSize: Theme.fontBody
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    Card {
        visible: !Audio.available
        icon: "dialog-warning"
        title: "Audio non disponibile"
        description: "Serve pactl (PipeWire o PulseAudio)."
    }

    CardGroup {
        visible: Audio.available
        title: "Output"
        Text {
            text: "Scegli dove riprodurre l'audio"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            bottomPadding: 4
        }
        Repeater {
            model: Audio.outputs
            delegate: DeviceCard { kind: "output" }
        }
        VolumeCard {
            visible: page.defaultOutput !== undefined
            device: page.defaultOutput
            kind: "output"
        }
    }

    CardGroup {
        visible: Audio.available && Audio.inputs.length > 0
        title: "Input"
        Text {
            text: "Scegli un dispositivo per parlare o registrare"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            bottomPadding: 4
        }
        Repeater {
            model: Audio.inputs
            delegate: DeviceCard { kind: "input" }
        }
        VolumeCard {
            visible: page.defaultInput !== undefined
            device: page.defaultInput
            kind: "input"
        }
    }

    CardGroup {
        title: "Avanzate"
        LinkCard {
            visible: System.available("volume-mixer")
            icon: "view-media-equalizer"
            title: "Mixer volume"
            description: "Il volume di ogni app, e l'uscita che usa"
            onClicked: System.trigger("volume-mixer")
        }
    }
}
