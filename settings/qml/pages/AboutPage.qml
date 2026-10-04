import QtQuick

// Sistema > Informazioni: il dispositivo e il sistema, come Windows 11.
Page {
    id: page

    Item {
        width: parent.width
        height: 84
        Image {
            id: deviceIcon
            anchors.verticalCenter: parent.verticalCenter
            width: 64
            height: 64
            source: Theme.icons + About.chassisIcon
            sourceSize: Qt.size(width, height)
        }
        Column {
            anchors { left: deviceIcon.right; leftMargin: 16; verticalCenter: parent.verticalCenter }
            Text {
                text: About.hostname
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.weight: Font.DemiBold
            }
            Text {
                text: About.model
                visible: text !== ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
        Button {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: "Rinomina questo PC"
            onClicked: {
                nameField.text = About.hostname
                renameDialog.open()
                nameField.forceActiveFocus()
                nameField.selectAll()
            }
        }
    }

    component SpecGroup: Card {
        id: spec
        property var rows: []
        property string header
        icon: "help-about"
        title: header
        trailing: Button {
            text: "Copia"
            onClicked: {
                copyHelper.text = About.asText()
                copyHelper.selectAll()
                copyHelper.copy()
            }
        }
        contentItem: Column {
            width: parent.width
            spacing: 6
            Repeater {
                model: spec.rows
                delegate: Row {
                    required property var modelData
                    width: parent.width
                    Text {
                        width: 200
                        leftPadding: 36
                        text: modelData.label
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                    }
                    Text {
                        width: parent.width - 200
                        text: modelData.value
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // Per "Copia": il testo passa dagli appunti attraverso un campo nascosto.
    TextEdit { id: copyHelper; visible: false }

    SpecGroup {
        header: "Specifiche dispositivo"
        rows: About.device
    }
    SpecGroup {
        icon: "start-here"
        header: "Specifiche sistema"
        rows: About.system
    }

    CardGroup {
        title: "Impostazioni correlate"
        LinkCard {
            visible: System.available("system")
            icon: "hwinfo"
            title: "Informazioni dettagliate sul sistema"
            description: "Hardware, driver, firmware"
            onClicked: System.trigger("system")
        }
        LinkCard {
            visible: System.available("task-manager")
            icon: "utilities-system-monitor"
            title: "Gestione attività"
            description: "Processi, prestazioni, uso di CPU e memoria"
            onClicked: System.trigger("task-manager")
        }
    }

    Dialog {
        id: renameDialog
        parent: root.contentItem
        title: "Rinomina il PC"
        primaryText: "Avanti"
        primaryEnabled: /^[A-Za-z0-9][A-Za-z0-9-]{0,62}$/.test(nameField.text)
        onAccepted: About.rename(nameField.text)
        Column {
            width: parent.width
            spacing: 12
            Text {
                width: parent.width
                text: "Il nome attuale del PC è " + About.hostname + ". Puoi usare lettere, numeri e trattini."
                color: Theme.text
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }
            TextBox {
                id: nameField
                width: parent.width
                Keys.onReturnPressed: if (renameDialog.primaryEnabled) { renameDialog.close(); About.rename(text) }
            }
        }
    }

    Connections {
        target: About
        function onRenameFailed(message) {
            errorText.text = message
            errorDialog.open()
        }
    }
    Dialog {
        id: errorDialog
        parent: root.contentItem
        title: "Impossibile rinominare il PC"
        primaryText: ""
        secondaryText: "Chiudi"
        Text {
            id: errorText
            width: parent.width
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }
}
