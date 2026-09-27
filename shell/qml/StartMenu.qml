import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// Il menu Start: ricerca in alto, tutte le app sotto, utente in fondo.
// Si apre salendo e sfumando; si chiude al clic fuori o con Esc.
Window {
    id: root
    objectName: "startMenu"

    visible: false
    width: 660
    // Su schermi bassi si accorcia: la ricerca in alto deve restare visibile.
    height: Math.min(732, Screen.height - Theme.taskbarHeight - 12)
    color: "transparent"

    property bool closing: false

    function open() {
        closing = false
        Apps.query = ""
        search.text = ""
        grid.currentIndex = 0
        grid.positionViewAtBeginning()
        panel.opacity = 0
        panel.y = panel.restY + panel.slide
        visible = true
        Shell.startMenuOpen = true
        openAnimation.restart()
        search.forceActiveFocus()
    }

    function close() {
        if (!visible || closing) {
            return
        }
        closing = true
        Shell.startMenuOpen = false
        openAnimation.stop()
        closeAnimation.restart()
    }

    function launch(row) {
        if (row >= 0 && row < grid.count && Apps.launch(row)) {
            close()
        }
    }

    Connections {
        target: Shell
        function onToggleStartRequested() {
            if (root.visible && !root.closing) {
                root.close()
            } else {
                root.open()
            }
        }
    }

    // Clic su una finestra o altrove: il menu perde il focus e si chiude,
    // come su Windows.
    onActiveChanged: {
        if (!active && visible) {
            close()
        }
    }

    ParallelAnimation {
        id: openAnimation
        NumberAnimation {
            target: panel; property: "y"; to: panel.restY
            duration: Theme.slow
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
        }
        NumberAnimation {
            target: panel; property: "opacity"; to: 1
            duration: Theme.normal
            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate
        }
    }

    SequentialAnimation {
        id: closeAnimation
        ParallelAnimation {
            NumberAnimation {
                target: panel; property: "y"; to: panel.restY + panel.slide
                duration: Theme.normal
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.accelerate
            }
            NumberAnimation {
                target: panel; property: "opacity"; to: 0
                duration: Theme.normal
                easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.accelerate
            }
        }
        ScriptAction {
            script: {
                root.visible = false
                root.closing = false
            }
        }
    }

    // Ombra morbida sotto il pannello. L'effetto lavora su una sagoma
    // nascosta con la stessa forma, così il pannello resta interattivo.
    Rectangle {
        id: shadowShape
        visible: false
        x: panel.x
        y: panel.y
        width: panel.width
        height: panel.height
        radius: panel.radius
        color: "black"
    }

    MultiEffect {
        source: shadowShape
        anchors.fill: shadowShape
        shadowEnabled: true
        shadowColor: "#000000"
        shadowOpacity: 0.5
        shadowBlur: 1.0
        shadowVerticalOffset: 6
        opacity: panel.opacity
    }

    Rectangle {
        id: panel

        readonly property real restY: 10
        // Di quanto sale aprendosi. La parte che sporge sotto la finestra
        // viene tagliata al bordo della taskbar.
        readonly property real slide: 140
        x: 10
        y: restY
        width: root.width - 20
        height: root.height - 32 // 10 sopra, 22 sotto: 12 di stacco dalla taskbar
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke

        // --- ricerca ---
        Rectangle {
            id: searchBox
            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 24 }
            height: 38
            radius: height / 2
            color: Theme.surfaceRaised
            border.width: search.activeFocus ? 1 : 0
            border.color: Theme.accent

            Text {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                visible: search.text.length === 0
                text: qsTr("Cerca app")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }

            TextInput {
                id: search
                anchors { left: parent.left; right: parent.right; leftMargin: 18; rightMargin: 18; verticalCenter: parent.verticalCenter }
                color: Theme.text
                selectionColor: Theme.accent
                font.pixelSize: Theme.fontNormal
                clip: true

                onTextChanged: {
                    Apps.query = text
                    grid.currentIndex = 0
                    grid.positionViewAtBeginning()
                }

                // La tastiera resta nella ricerca; le frecce muovono la griglia.
                Keys.onEscapePressed: root.close()
                Keys.onReturnPressed: root.launch(grid.currentIndex)
                Keys.onEnterPressed: root.launch(grid.currentIndex)
                Keys.onDownPressed: grid.moveCurrentIndexDown()
                Keys.onUpPressed: grid.moveCurrentIndexUp()
                Keys.onTabPressed: grid.moveCurrentIndexRight()
                Keys.onBacktabPressed: grid.moveCurrentIndexLeft()
            }
        }

        Text {
            id: sectionTitle
            anchors { top: searchBox.bottom; left: parent.left; topMargin: 20; leftMargin: 32 }
            text: search.text.length > 0 ? qsTr("Risultati") : qsTr("Tutte le app")
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
        }

        // --- griglia delle app ---
        GridView {
            id: grid
            anchors {
                top: sectionTitle.bottom; bottom: footer.top
                left: parent.left; right: parent.right
                topMargin: 10; leftMargin: 20; rightMargin: 20; bottomMargin: 8
            }
            clip: true
            model: Apps
            cellWidth: Math.floor(width / 6)
            cellHeight: 96
            keyNavigationWraps: true
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: false

            // Con le frecce la selezione può uscire dalla vista: la seguiamo.
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)

            delegate: AppTile {
                width: grid.cellWidth
                height: grid.cellHeight
                current: index === grid.currentIndex && search.text.length > 0
                onActivated: root.launch(index)
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: qsTr("Nessuna app trovata")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
        }

        // --- piè di pagina: utente ---
        Rectangle {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 60
            color: "transparent"

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: Theme.stroke
            }

            Row {
                anchors { left: parent.left; leftMargin: 32; verticalCenter: parent.verticalCenter }
                spacing: 12

                Rectangle {
                    width: 32
                    height: 32
                    radius: 16
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: Shell.userInitial
                        color: "white"
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Shell.userName
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                }
            }

            // Esci dalla sessione, dove Windows ha il pulsante di
            // accensione. Chiude Vela, non il computer.
            Item {
                id: logoutButton
                anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
                width: 40
                height: 40

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: logoutMouse.pressed ? Theme.pressed : Theme.hover
                    opacity: logoutMouse.containsMouse ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
                    }
                }

                // Una porta aperta e una freccia che esce.
                Shape {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    preferredRendererType: Shape.CurveRenderer
                    scale: logoutMouse.pressed ? 0.9 : 1
                    Behavior on scale {
                        NumberAnimation { duration: Theme.normal; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
                    }

                    ShapePath {
                        strokeColor: Theme.text
                        strokeWidth: 1.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        // Stipite: da destra in alto, a sinistra, giù, a destra.
                        startX: 11; startY: 3
                        PathLine { x: 4; y: 3 }
                        PathLine { x: 4; y: 17 }
                        PathLine { x: 11; y: 17 }
                    }
                    ShapePath {
                        strokeColor: Theme.text
                        strokeWidth: 1.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        startX: 8.5; startY: 10
                        PathLine { x: 17; y: 10 }
                        PathMove { x: 13.5; y: 6.5 }
                        PathLine { x: 17; y: 10 }
                        PathLine { x: 13.5; y: 13.5 }
                    }
                }

                // Il nome dell'azione, al passaggio del mouse.
                Rectangle {
                    anchors { bottom: parent.top; bottomMargin: 6; horizontalCenter: parent.horizontalCenter }
                    width: logoutLabel.implicitWidth + 16
                    height: logoutLabel.implicitHeight + 8
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.stroke
                    opacity: logoutMouse.containsMouse ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.fast; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
                    }
                    Text {
                        id: logoutLabel
                        anchors.centerIn: parent
                        text: "Esci"
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                    }
                }

                MouseArea {
                    id: logoutMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: Shell.logout()
                }
            }
        }
    }
}
