import QtQuick

// La finestra Proprietà di un file, una cartella o più elementi, come in
// Windows: Generale (nome, tipo, apri con, percorso, dimensioni, date,
// attributi), Autorizzazioni (al posto di "Sicurezza": lettura, scrittura,
// esecuzione per proprietario, gruppo e altri) e Dettagli. OK applica e
// chiude, Applica applica e resta, Annulla chiude.
Window {
    id: root
    objectName: "propertiesDialog"
    visible: false
    width: 440
    height: 600
    color: "transparent"

    property var paths: []
    property var info: ({})
    property int tab: 0
    // Le modifiche non ancora applicate.
    property string newName: ""
    property bool readOnly: false
    property int permissions: 0
    readonly property bool single: paths.length === 1
    readonly property bool changed: (single && newName !== info.name)
        || readOnly !== info.readOnly
        || (single && permissions !== info.permissions)
    property string sizeText: ""
    property string diskText: ""
    property string contentText: ""
    property string defaultApp: ""
    property string defaultAppIcon: ""

    Connections {
        target: Menus
        function onPropertiesRequested(paths) {
            root.load(paths)
            root.tab = 0
            root.visible = true
            panel.forceActiveFocus()
        }
    }
    Connections {
        target: Properties
        function onSizeCounted(token, bytes, onDisk, files, folders) {
            if (token === root.info.token) {
                root.showSizes(bytes, onDisk, files, folders)
            }
        }
    }

    function load(list) {
        paths = list
        info = Properties.describe(list)
        newName = info.name
        readOnly = info.readOnly
        permissions = info.permissions || 0
        if (info.bytes !== undefined) {
            showSizes(info.bytes, info.onDisk, info.files, info.folders)
        } else {
            sizeText = "Calcolo in corso…"
            diskText = sizeText
            contentText = sizeText
        }
        defaultApp = ""
        defaultAppIcon = ""
        if (single && !info.isDir) {
            const apps = Apps.appsForFile(list[0])
            const preferred = apps.find(a => a.isDefault) || apps[0]
            if (preferred) {
                defaultApp = preferred.name
                defaultAppIcon = preferred.icon
            }
        }
    }

    function showSizes(bytes, onDisk, files, folders) {
        sizeText = Properties.formatSize(bytes)
        diskText = Properties.formatSize(onDisk)
        contentText = files + (files === 1 ? " file, " : " file, ") + folders + (folders === 1 ? " cartella" : " cartelle")
    }

    function apply() {
        if (readOnly !== info.readOnly) {
            Properties.setReadOnly(paths, readOnly)
        }
        if (single && permissions !== info.permissions) {
            Properties.setPermissions(paths[0], permissions)
        }
        if (single && newName !== info.name && Desktop.rename(paths[0], newName)) {
            const dir = paths[0].substring(0, paths[0].lastIndexOf("/") + 1)
            paths = [dir + newName]
        }
        load(paths)
    }

    // Dove sta la finestra (al centro dello schermo): per i menu.
    function screenPoint(item, x, y) {
        const p = item.mapToItem(null, x, y)
        return Qt.point((Screen.width - root.width) / 2 + p.x, (Screen.height - root.height) / 2 + p.y)
    }

    onActiveChanged: {
        if (!active && visible && !Menus.isOpen) {
            visible = false
        }
    }

    component Label: Text {
        color: Theme.textDim
        font.pixelSize: Theme.fontNormal
        width: 120
    }
    component Value: Text {
        color: Theme.text
        font.pixelSize: Theme.fontNormal
        width: 260
        elide: Text.ElideMiddle
    }
    component Line: Rectangle {
        width: 392
        height: 1
        color: Theme.stroke
    }
    component Check: Item {
        id: check
        property string label
        property bool checked
        property bool usable: true
        signal toggled()
        width: box.width + 8 + text.implicitWidth
        height: 22
        opacity: usable ? 1 : 0.5
        Rectangle {
            id: box
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: check.checked ? Theme.accent : "transparent"
            border.width: check.checked ? 0 : 1
            border.color: Theme.textDim
            Text {
                anchors.centerIn: parent
                text: "✓"
                visible: check.checked
                color: "white"
                font.pixelSize: 12
            }
        }
        Text {
            id: text
            anchors { left: box.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
            text: check.label
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
        MouseArea {
            anchors.fill: parent
            enabled: check.usable
            onClicked: check.toggled()
        }
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke
        focus: true
        Keys.onEscapePressed: root.visible = false
        Keys.onReturnPressed: { root.apply(); root.visible = false }

        Text {
            x: 24
            y: 16
            text: "Proprietà - " + (root.info.name || "")
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
            width: parent.width - 48
            elide: Text.ElideMiddle
        }

        // Schede
        Row {
            x: 20
            y: 44
            spacing: 4
            Repeater {
                model: root.single ? ["Generale", "Autorizzazioni", "Dettagli"] : ["Generale"]
                delegate: Item {
                    required property string modelData
                    required property int index
                    width: tabLabel.implicitWidth + 24
                    height: 34
                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: root.tab === parent.index ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.weight: root.tab === parent.index ? Font.DemiBold : Font.Normal
                    }
                    Rectangle {
                        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                        width: 20
                        height: 3
                        radius: 1.5
                        color: Theme.accent
                        visible: root.tab === parent.index
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.tab = parent.index
                    }
                }
            }
        }

        // --- Generale ---
        Column {
            visible: root.tab === 0
            x: 24
            y: 92
            spacing: 12

            Row {
                spacing: 16
                Image {
                    width: 40
                    height: 40
                    source: "image://icon/" + encodeURIComponent(root.info.icon || "unknown")
                    sourceSize: Qt.size(width, height)
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 336
                    height: 32
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.width: 1
                    border.color: nameField.activeFocus ? Theme.accent : Theme.stroke
                    MenuTextField {
                        id: nameField
                        anchors { left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        text: root.newName
                        readOnly: !root.single
                        mapToScreen: (x, y) => root.screenPoint(nameField, x, y)
                        onTextEdited: root.newName = text
                    }
                }
            }
            Line {}
            Row { Label { text: "Tipo:" } Value { text: root.info.type || "" } }
            Row {
                visible: root.defaultApp !== ""
                height: 28
                Label { text: "Apri con:"; anchors.verticalCenter: parent.verticalCenter }
                Row {
                    width: 260
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Image {
                        width: 16
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        source: root.defaultAppIcon !== "" ? "image://icon/" + encodeURIComponent(root.defaultAppIcon) : ""
                        sourceSize: Qt.size(width, height)
                    }
                    Text {
                        width: 150
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.defaultApp
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: changeButton
                        width: 80
                        height: 28
                        radius: Theme.radiusSmall
                        color: changeMouse.containsMouse ? Theme.hover : Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.stroke
                        Text { anchors.centerIn: parent; text: "Cambia…"; color: Theme.text; font.pixelSize: Theme.fontSmall }
                        MouseArea {
                            id: changeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                const path = root.paths[0]
                                const p = root.screenPoint(changeButton, 0, changeButton.height + 2)
                                Menus.open(Apps.appsForFile(path).map(a => ({
                                    text: a.name.replace(/&/g, "&&"), icon: a.icon, radio: true, checked: a.isDefault,
                                    action: () => { Properties.setDefaultApp(path, a.id); root.defaultApp = a.name; root.defaultAppIcon = a.icon }
                                })), p.x, p.y)
                            }
                        }
                    }
                }
            }
            Line {}
            Row { Label { text: "Percorso:" } Value { text: root.info.location || "" } }
            Row { Label { text: "Dimensioni:" } Value { text: root.sizeText } }
            Row { Label { text: "Su disco:" } Value { text: root.diskText } }
            Row {
                visible: !!root.info.hasFolders || root.paths.length > 1
                Label { text: "Contenuto:" }
                Value { text: root.contentText }
            }
            Line { visible: root.single }
            Row { visible: root.single && !!root.info.created; Label { text: "Creato:" } Value { text: root.info.created || "" } }
            Row { visible: root.single; Label { text: "Ultima modifica:" } Value { text: root.info.modified || "" } }
            Row { visible: root.single; Label { text: "Ultimo accesso:" } Value { text: root.info.accessed || "" } }
            Line {}
            Row {
                spacing: 16
                Label { text: "Attributi:"; width: 104; anchors.verticalCenter: parent.verticalCenter }
                Check {
                    label: "Sola lettura"
                    checked: root.readOnly
                    onToggled: root.readOnly = !root.readOnly
                }
                // Su Linux "nascosto" è il punto all'inizio del nome.
                Check {
                    label: "Nascosto"
                    checked: !!root.info.hidden
                    usable: false
                }
            }
        }

        // --- Autorizzazioni ---
        Column {
            visible: root.tab === 1
            x: 24
            y: 92
            spacing: 14

            Row { Label { text: "Proprietario:" } Value { text: root.info.owner || "" } }
            Row { Label { text: "Gruppo:" } Value { text: root.info.group || "" } }
            Line {}
            Text {
                text: root.info.mine ? "Chi può fare cosa:" : "Solo il proprietario può cambiare le autorizzazioni."
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
            Repeater {
                model: [{ who: "Proprietario", shift: 6 }, { who: "Gruppo", shift: 3 }, { who: "Altri", shift: 0 }]
                delegate: Row {
                    required property var modelData
                    spacing: 16
                    Label { text: modelData.who; width: 104 }
                    Repeater {
                        model: [{ what: "Lettura", bit: 4 }, { what: "Scrittura", bit: 2 }, { what: root.info.isDir ? "Apertura" : "Esecuzione", bit: 1 }]
                        delegate: Check {
                            required property var modelData
                            readonly property int mask: modelData.bit << parent.modelData.shift
                            label: modelData.what
                            usable: !!root.info.mine
                            checked: (root.permissions & mask) !== 0
                            onToggled: root.permissions = root.permissions ^ mask
                        }
                    }
                }
            }
        }

        // --- Dettagli ---
        Column {
            visible: root.tab === 2
            x: 24
            y: 92
            spacing: 12
            Row { Label { text: "Nome:" } Value { text: root.info.name || "" } }
            Row { Label { text: "Tipo MIME:" } Value { text: root.info.mimeName || "" } }
            Row { visible: !!root.info.imageSize; Label { text: "Dimensioni:" } Value { text: (root.info.imageSize || "") + " pixel" } }
            Row { Label { text: "Dimensioni file:" } Value { text: root.sizeText } }
        }

        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 20 }
            spacing: 8

            component DialogButton: Rectangle {
                id: button
                property string label
                property bool primary: false
                property bool usable: true
                signal clicked()
                width: 96
                height: 32
                radius: Theme.radiusSmall
                opacity: usable ? 1 : 0.5
                color: primary ? (mouse.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent)
                               : (mouse.pressed ? Theme.pressed : mouse.containsMouse ? Theme.hover : Theme.surfaceRaised)
                border.width: primary ? 0 : 1
                border.color: Theme.stroke
                Text {
                    anchors.centerIn: parent
                    text: button.label
                    color: button.primary ? "white" : Theme.text
                    font.pixelSize: Theme.fontNormal
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: button.usable
                    onClicked: button.clicked()
                }
            }

            DialogButton {
                label: "OK"
                primary: true
                onClicked: { root.apply(); root.visible = false }
            }
            DialogButton {
                label: "Annulla"
                onClicked: root.visible = false
            }
            DialogButton {
                label: "Applica"
                usable: root.changed
                onClicked: root.apply()
            }
        }
    }
}
