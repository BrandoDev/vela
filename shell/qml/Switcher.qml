import QtQuick

// Alt+Tab: le finestre aperte con anteprima, dalla più recente. La tastiera
// la gestisce il compositor, che ci dice cosa mostrare e cosa è selezionato:
// qui si disegna e basta.
Window {
    id: root
    objectName: "switcher"

    visible: false
    width: Screen.width
    height: Screen.height
    color: "transparent"

    property int selected: 0

    readonly property int cardWidth: 272
    readonly property int cardHeight: 212
    readonly property int spacing: 8
    readonly property int perRow: Math.max(1, Math.min(windows.count,
        Math.floor((width - 96 + spacing) / (cardWidth + spacing))))

    ListModel { id: windows }

    // Come su Windows: un Alt+Tab veloce cambia finestra senza far
    // lampeggiare il pannello.
    Timer {
        id: showDelay
        interval: 100
        onTriggered: {
            root.visible = true
            appear.restart()
        }
    }

    Connections {
        target: Shell
        function onSwitcherShown(selected, identifiers) {
            windows.clear()
            for (const id of identifiers) {
                windows.append({
                    identifier: id,
                    title: Capture.title(id),
                    icon: Apps.iconForAppId(Capture.appId(id)),
                    revision: 0
                })
            }
            root.selected = selected
            Capture.capture(identifiers)
            showDelay.restart()
        }
        function onSwitcherSelected(selected) {
            root.selected = selected
        }
        function onSwitcherHidden() {
            showDelay.stop()
            root.visible = false
        }
    }

    Connections {
        target: Capture
        function onThumbnailReady(identifier) {
            for (let i = 0; i < windows.count; ++i) {
                if (windows.get(i).identifier === identifier) {
                    windows.setProperty(i, "revision", windows.get(i).revision + 1)
                }
            }
        }
    }

    ParallelAnimation {
        id: appear
        NumberAnimation { target: panel; property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        NumberAnimation {
            target: panel; property: "scale"; from: 0.96; to: 1; duration: Theme.normal
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
        }
    }

    // Lo sfondo sfocato solo sotto il pannello (la finestra copre lo schermo).
    function updateBlur() {
        Effects.setBlur(root, [Qt.rect(panel.x, panel.y, panel.width, panel.height)])
    }

    Rectangle {
        id: panel
        onXChanged: root.updateBlur()
        onYChanged: root.updateBlur()
        onWidthChanged: root.updateBlur()
        onHeightChanged: root.updateBlur()
        anchors.centerIn: parent
        width: flow.width + 32
        height: flow.height + 32
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke

        Flow {
            id: flow
            anchors.centerIn: parent
            width: root.perRow * root.cardWidth + (root.perRow - 1) * root.spacing
            spacing: root.spacing

            Repeater {
                model: windows

                Rectangle {
                    id: card
                    required property int index
                    required property string identifier
                    required property string title
                    required property string icon
                    required property int revision
                    readonly property bool current: index === root.selected

                    width: root.cardWidth
                    height: root.cardHeight
                    radius: Theme.radiusLarge
                    color: current || cardMouse.containsMouse ? Theme.hover : "transparent"
                    border.width: current ? 2 : 0
                    border.color: Theme.accent

                    // Un clic su un'anteprima ci porta subito.
                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Shell.pickSwitcher(card.index)
                    }

                    // Icona e titolo sopra l'anteprima.
                    Row {
                        id: header
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8

                        Image {
                            width: 16
                            height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            source: Theme.icons + encodeURIComponent(card.icon)
                            sourceSize: Qt.size(width, height)
                        }
                        Text {
                            width: header.width - 24
                            anchors.verticalCenter: parent.verticalCenter
                            text: card.title
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }

                    Item {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; top: header.bottom; margins: 12 }

                        Image {
                            id: thumbnail
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            source: card.revision > 0 ? "image://thumbnail/" + card.identifier + "/" + card.revision : ""
                            cache: false
                            smooth: true
                        }
                        // Finché l'anteprima non c'è: l'icona grande.
                        Image {
                            anchors.centerIn: parent
                            width: 48
                            height: 48
                            visible: thumbnail.status !== Image.Ready
                            source: Theme.icons + encodeURIComponent(card.icon)
                            sourceSize: Qt.size(width, height)
                        }
                    }
                }
            }
        }
    }
}
