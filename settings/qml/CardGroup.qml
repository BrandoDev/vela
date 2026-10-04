import QtQuick

// Un gruppo di schede con il suo titolo, come le sezioni di Windows 11
// ("Scala e layout", "Impostazioni correlate"...): schede staccate di poco.
Column {
    id: group
    property string title
    default property alias cards: cardColumn.data
    width: parent ? parent.width : 0
    spacing: 0

    Text {
        visible: group.title !== ""
        text: group.title
        color: Theme.text
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
        topPadding: 20
        bottomPadding: 8
    }
    Column {
        id: cardColumn
        width: parent.width
        spacing: 4
    }
}
