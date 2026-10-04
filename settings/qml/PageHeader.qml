import QtQuick

// Il titolo della pagina con il percorso, come Windows 11:
// "Sistema  ›  Schermo", dove i pezzi precedenti sono link.
Row {
    id: header
    property var crumbs: [] // [{name, title}]
    signal navigate(string name)
    spacing: 12

    Repeater {
        model: header.crumbs
        delegate: Row {
            required property var modelData
            required property int index
            readonly property bool current: index === header.crumbs.length - 1
            spacing: 12
            Text {
                id: crumb
                text: modelData.title
                color: parent.current ? Theme.text : crumbMouse.containsMouse ? Theme.textSecondary : Theme.textTertiary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                MouseArea {
                    id: crumbMouse
                    anchors.fill: parent
                    enabled: !parent.parent.current
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: header.navigate(modelData.name)
                }
            }
            Text {
                visible: !parent.current
                anchors.verticalCenter: crumb.verticalCenter
                text: "›"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontTitle - 4
            }
        }
    }
}
