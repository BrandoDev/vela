import QtQuick

// Il riquadro a destra di Esplora, come Windows 11. Due modi:
// - "preview" (Alt+P), il riquadro di anteprima: l'immagine, l'inizio di un
//   file di testo, la miniatura di video e PDF;
// - "details" (Alt+Maiusc+P), il riquadro dei dettagli: icona, nome, tipo e
//   proprietà dell'elemento scelto (o della cartella aperta).
Item {
    id: pane
    property var tab
    property string mode: "preview"
    readonly property var model: tab.model

    // L'elemento scelto: uno solo; con più elementi se ne dice il numero.
    readonly property int count: model ? model.selectionCount : 0
    readonly property string path: model && count === 1 ? (model.selectionVersion, model.selectedPaths()[0] || "") : ""
    readonly property var info: path !== "" ? Ops.details(path)
        : mode === "details" && count === 0 && tab.isFolder ? Ops.details(tab.location) : null
    readonly property bool file: !!info && !info.isDir
    readonly property string text: mode === "preview" && file ? Ops.previewText(path) : ""
    readonly property bool image: mode === "preview" && file && text === "" && Ops.isImage(path)

    clip: true

    function dateText(d) { return d ? Qt.formatDateTime(d, "dd/MM/yyyy HH:mm") : "" }

    // ------------------------------------------------------ anteprima --
    Item {
        anchors { fill: parent; margins: 16 }
        visible: pane.mode === "preview"

        Text {
            anchors.centerIn: parent
            width: parent.width
            visible: !pane.file
            text: pane.count > 1 ? pane.count + " elementi selezionati"
                : pane.info && pane.info.isDir ? "Seleziona un file da visualizzare in anteprima."
                : "Seleziona un file da visualizzare in anteprima."
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        // Un'immagine, intera.
        Image {
            anchors.fill: parent
            visible: pane.image
            source: pane.image ? "file://" + encodeURI(pane.path).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
            sourceSize: Qt.size(width, height) // logico: Qt lo porta in pixel
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            autoTransform: true
            smooth: true
            mipmap: true
        }
        // Un file di testo: l'inizio, com'è.
        Flickable {
            anchors.fill: parent
            visible: pane.text !== ""
            contentHeight: textView.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Text {
                id: textView
                width: parent.width
                text: pane.text
                textFormat: Text.PlainText
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WrapAnywhere
            }
        }
        // Gli altri: la miniatura (video, PDF...) o l'icona grande.
        Column {
            anchors.centerIn: parent
            width: parent.width
            visible: pane.file && !pane.image && pane.text === ""
            spacing: 12
            Item {
                width: parent.width
                height: Math.min(parent.width, 320)
                Image {
                    id: thumb
                    anchors.fill: parent
                    source: parent.parent.visible
                        ? "image://filethumb/" + encodeURIComponent(pane.path) + "/" + (pane.info.modified ? pane.info.modified.getTime() : 0) : ""
                    sourceSize: Qt.size(512, 512)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                }
                Image {
                    anchors.centerIn: parent
                    visible: thumb.status !== Image.Ready
                    width: 96
                    height: 96
                    source: pane.file ? "image://fileicon/" + encodeURIComponent(pane.info.icon) : ""
                    sourceSize: Qt.size(width, height)
                }
            }
            Text {
                width: parent.width
                text: pane.file ? "Nessuna anteprima disponibile." : ""
                visible: thumb.status !== Image.Ready
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }

    // -------------------------------------------------------- dettagli --
    Flickable {
        anchors { fill: parent; margins: 16 }
        visible: pane.mode === "details"
        contentHeight: details.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: details
            width: parent.width
            spacing: 12

            Text {
                visible: pane.count > 1
                width: parent.width
                text: pane.count + " elementi selezionati"
                color: Theme.text
                font.pixelSize: Theme.fontNormal + 2
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                visible: pane.count > 1
                text: "Dimensione totale: " + (pane.model ? Ops.formatSize(pane.model.selectionSize) : "")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }

            // Un elemento (o la cartella aperta).
            Item {
                visible: !!pane.info && pane.count <= 1
                width: parent.width
                height: 128
                Image {
                    id: detailsThumb
                    anchors.fill: parent
                    source: pane.file && pane.info ? "image://filethumb/" + encodeURIComponent(pane.path) + "/"
                        + (pane.info.modified ? pane.info.modified.getTime() : 0) : ""
                    sourceSize: Qt.size(256, 256)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                }
                Image {
                    anchors.centerIn: parent
                    visible: detailsThumb.status !== Image.Ready
                    width: 96
                    height: 96
                    source: pane.info ? "image://fileicon/" + encodeURIComponent(pane.info.icon) : ""
                    sourceSize: Qt.size(width, height)
                }
            }
            Text {
                visible: !!pane.info && pane.count <= 1
                width: parent.width
                text: pane.info ? (pane.count === 0 && pane.tab.isFolder ? pane.tab.title : pane.info.name) : ""
                color: Theme.text
                font.pixelSize: Theme.fontNormal + 2
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAnywhere
            }
            Text {
                visible: !!pane.info && pane.count <= 1
                width: parent.width
                text: pane.info ? pane.info.type : ""
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }
            Rectangle { visible: !!pane.info && pane.count <= 1; width: parent.width; height: 1; color: Theme.divider }
            Text {
                visible: !!pane.info && pane.count <= 1
                text: "Proprietà"
                color: Theme.text
                font.pixelSize: Theme.fontNormal
                font.weight: Font.DemiBold
            }
            Repeater {
                model: {
                    const i = pane.info
                    if (!i || pane.count > 1) return []
                    const rows = []
                    if (i.sizeText) rows.push(["Dimensione", i.sizeText])
                    if (i.width) rows.push(["Dimensioni", i.width + " x " + i.height])
                    if (i.items !== undefined) rows.push(["Elementi", String(i.items)])
                    rows.push(["Ultima modifica", pane.dateText(i.modified)])
                    rows.push(["Data creazione", pane.dateText(i.created)])
                    if (i.target) rows.push(["Destinazione", i.target])
                    rows.push(["Percorso", i.location])
                    return rows
                }
                delegate: Column {
                    required property var modelData
                    width: details.width
                    spacing: 2
                    Text {
                        text: modelData[0]
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }
                    Text {
                        width: parent.width
                        text: modelData[1]
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
