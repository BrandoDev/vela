import QtQuick
import QtQuick.Shapes

// Le notifiche, in basso a destra sopra la taskbar come su Windows 11. La
// finestra è alta quanto i riquadri: il resto dello schermo resta cliccabile.
Window {
    id: root
    objectName: "notifications"
    width: 380
    height: Math.max(1, stack.implicitHeight + 24)
    visible: Notifications.count > 0
    color: "transparent"

    // Lo sfondo sfocato sotto i riquadri: la finestra intera, la forma la
    // danno i riquadri (fuori è trasparente).
    function updateBlur() {
        Effects.setBlur(root, [Qt.rect(0, 0, width, height)])
    }
    onWidthChanged: updateBlur()
    onHeightChanged: updateBlur()
    Component.onCompleted: updateBlur()

    // Mouse sopra: non scadono mentre le si legge.
    HoverHandler {
        onHoveredChanged: Notifications.setHovered(hovered)
    }

    Column {
        id: stack
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Repeater {
            model: Notifications

            delegate: Rectangle {
                id: toast

                required property int notificationId
                required property string appName
                required property string icon
                required property string summary
                required property string body
                required property var actions
                required property bool hasDefaultAction
                required property bool critical

                width: stack.width
                height: content.implicitHeight + 28
                radius: Theme.radiusLarge
                color: Theme.popup
                border.width: 1
                border.color: critical ? Qt.rgba(1, 0.35, 0.35, 0.6) : Theme.stroke

                // Entra da destra, come su Windows.
                transform: Translate { id: slide; x: 60 }
                opacity: 0
                Component.onCompleted: appear.start()
                ParallelAnimation {
                    id: appear
                    NumberAnimation {
                        target: slide; property: "x"; to: 0
                        duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
                    }
                    NumberAnimation {
                        target: toast; property: "opacity"; to: 1
                        duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
                    }
                }

                MouseArea {
                    id: toastMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    // Clic: apre l'app (l'azione "default"), altrimenti chiude.
                    onClicked: toast.hasDefaultAction ? Notifications.invoke(toast.notificationId, "default")
                                                      : Notifications.dismiss(toast.notificationId)
                }

                Column {
                    id: content
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 6

                    // App: icona e nome, piccoli, come intestazione.
                    Row {
                        spacing: 8
                        Image {
                            width: 16
                            height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            source: toast.icon
                            sourceSize: Qt.size(width, height)
                            smooth: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: toast.appName
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                    }

                    Text {
                        width: parent.width - 20
                        text: toast.summary
                        visible: text !== ""
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: toast.body
                        visible: text !== ""
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        textFormat: Text.StyledText // <b>, <i>, <a> dalla specifica
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        linkColor: Theme.accentLight
                        onLinkActivated: link => Qt.openUrlExternally(link)
                    }

                    // Le azioni dell'app ("Rispondi", "Segna come letto"...).
                    Row {
                        visible: toast.actions.length > 0
                        topPadding: 4
                        spacing: 8
                        Repeater {
                            model: toast.actions
                            delegate: Rectangle {
                                required property var modelData
                                width: Math.max(80, actionLabel.implicitWidth + 24)
                                height: 30
                                radius: Theme.radiusSmall
                                color: actionMouse.pressed ? Theme.pressed : actionMouse.containsMouse ? Theme.hover : Theme.surfaceRaised
                                border.width: 1
                                border.color: Theme.stroke
                                Text {
                                    id: actionLabel
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSmall
                                }
                                MouseArea {
                                    id: actionMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: Notifications.invoke(toast.notificationId, modelData.key)
                                }
                            }
                        }
                    }
                }

                // Chiudi: compare col mouse sopra, in alto a destra.
                Rectangle {
                    anchors { right: parent.right; top: parent.top; margins: 8 }
                    width: 24
                    height: 24
                    radius: Theme.radiusSmall
                    color: closeMouse.containsMouse ? Theme.hover : "transparent"
                    opacity: toastMouse.containsMouse || closeMouse.containsMouse ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.fast }
                    }
                    Shape {
                        anchors.centerIn: parent
                        width: 10
                        height: 10
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: Theme.text
                            strokeWidth: 1.2
                            capStyle: ShapePath.RoundCap
                            startX: 0; startY: 0
                            PathLine { x: 10; y: 10 }
                            PathMove { x: 10; y: 0 }
                            PathLine { x: 0; y: 10 }
                        }
                    }
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Notifications.dismiss(toast.notificationId)
                    }
                }
            }
        }
    }
}
