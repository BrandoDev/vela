import QtQuick

// App > App installate: cerca e disinstalla, come Windows 11.
Page {
    id: page
    Component.onDestruction: Apps.query = ""

    Item {
        width: parent.width
        height: 44
        TextBox {
            id: filter
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(360, parent.width)
            placeholderText: "Cerca app"
            onTextChanged: Apps.query = text
        }
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: Apps.count + (Apps.count === 1 ? " app trovata" : " app trovate")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }
    }

    Repeater {
        model: Apps
        delegate: Card {
            id: app
            required property string appId
            required property string name
            required property string iconName
            required property string comment
            icon: iconName
            title: name
            description: comment
            trailing: Button {
                subtle: true
                text: "Disinstalla"
                usable: System.canUninstall(Apps.desktopFile(app.appId))
                onClicked: {
                    uninstallDialog.appId = app.appId
                    uninstallDialog.appName = app.name
                    uninstallDialog.open()
                }
            }
        }
    }

    Dialog {
        id: uninstallDialog
        parent: root.contentItem
        property string appId
        property string appName
        title: "Disinstalla " + appName
        primaryText: "Disinstalla"
        onAccepted: System.uninstall(Apps.desktopFile(appId))
        Text {
            width: parent.width
            text: "L'app e le sue informazioni correlate verranno disinstallate. Potrebbe essere chiesta la password."
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }
}
