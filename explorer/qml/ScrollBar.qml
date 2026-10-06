// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// La barra di scorrimento sottile di Windows 11 per una Flickable non
// trascinabile (le viste dei file: il trascinamento serve a selezionare):
// si allarga al passaggio del mouse; la rotellina scorre la vista.
Item {
    id: bar
    property Flickable flickable
    readonly property real ratio: flickable && flickable.contentHeight > 0 ? Math.min(1, flickable.height / flickable.contentHeight) : 1
    visible: ratio < 1
    width: hover.hovered || drag.active ? 12 : 6
    Behavior on width { NumberAnimation { duration: Theme.fast } }

    function scrollBy(dy) {
        const max = Math.max(0, flickable.contentHeight - flickable.height + flickable.bottomMargin)
        flickable.contentY = Math.max(-flickable.topMargin, Math.min(max, flickable.contentY + dy))
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.scrollTrack
        visible: hover.hovered || drag.active
    }
    Rectangle {
        id: thumb
        x: 2
        width: parent.width - 4
        y: bar.flickable ? Math.max(0, Math.min(bar.height - height, bar.flickable.contentY / Math.max(1, bar.flickable.contentHeight) * bar.height)) : 0
        height: Math.max(24, bar.height * bar.ratio)
        radius: width / 2
        color: hover.hovered || drag.active ? Theme.scrollThumbHover : Theme.scrollThumb
    }
    HoverHandler { id: hover }
    MouseArea {
        id: drag
        anchors.fill: parent
        property real grab: 0
        property bool active: pressed
        onPressed: mouse => {
            grab = mouse.y - thumb.y
            if (grab < 0 || grab > thumb.height) {
                grab = thumb.height / 2
                move(mouse.y)
            }
        }
        onPositionChanged: mouse => move(mouse.y)
        function move(y) {
            const top = Math.max(0, Math.min(bar.height - thumb.height, y - grab))
            bar.flickable.contentY = top / bar.height * bar.flickable.contentHeight
        }
    }
}
