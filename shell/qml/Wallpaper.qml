import QtQuick

// Lo sfondo del desktop: una superficie layer-shell sotto a tutto, anche
// sotto la taskbar. La dimensione la decide il compositor (ancorata ai
// quattro lati).
Window {
    id: root
    objectName: "wallpaper"

    visible: false
    width: Screen.width
    height: Screen.height
    color: Theme.desktop // mentre l'immagine si prepara

    Image {
        anchors.fill: parent
        // Disegnata già alla dimensione esatta dello schermo, in pixel veri.
        sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio),
                            Math.round(height * Screen.devicePixelRatio))
        source: width > 0 && height > 0
            ? "image://wallpaper/" + encodeURIComponent(Shell.wallpaper)
            : ""
        asynchronous: true
        cache: false
        smooth: false // è già della dimensione giusta

        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
        }
    }

    // Il menu del desktop di Windows 11 (docs/renderer.md §14.9). Le voci
    // delle icone sul desktop (Visualizza, Ordina per, Aggiorna, Nuovo) ci
    // sono ma spente: arriveranno con le icone.
    function desktopEntries() {
        const off = (text, icon) => ({ text: text, icon: icon, enabled: false })
        const system = (text, icon, name) => ({ text: text, icon: icon, enabled: System.available(name), action: () => System.trigger(name) })
        return [
            { text: "&Visualizza", icon: "view-list-icons", children: [
                { text: "Icone &grandi", radio: true, checked: false, enabled: false },
                { text: "Icone &medie", radio: true, checked: true, enabled: false },
                { text: "Icone &piccole", radio: true, checked: false, enabled: false },
                { separator: true },
                { text: "&Disponi icone automaticamente", checked: false, enabled: false },
                { text: "&Allinea icone alla griglia", checked: true, enabled: false },
                { separator: true },
                { text: "Mostra &icone del desktop", checked: false, enabled: false }
            ] },
            { text: "&Ordina per", icon: "view-sort", children: [
                off("&Nome"), off("&Dimensione"), off("&Tipo elemento"), off("Data &ultima modifica")
            ] },
            off("A&ggiorna", "view-refresh"),
            { separator: true },
            { text: "&Nuovo", icon: "list-add", children: [
                off("&Cartella", "folder-new"), off("C&ollegamento", "insert-link"),
                { separator: true },
                off("Documento di &testo", "text-plain")
            ] },
            { separator: true },
            system("&Impostazioni schermo", "preferences-desktop-display", "display"),
            system("&Personalizza", "preferences-desktop-wallpaper", "personalize"),
            { separator: true },
            { text: "Apri in &Terminale", icon: "utilities-terminal", action: () => System.openTerminal(System.desktopDirectory()) },
            { separator: true },
            { text: "Mostra altre opzioni", icon: "view-more-symbolic", shortcut: "Maiusc+F10", enabled: false }
        ]
    }

    // Lo sfondo copre tutto lo schermo: le sue coordinate sono quelle dello schermo.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: mouse => Menus.open(root.desktopEntries(), mouse.x, mouse.y, { screen: root.screen ? root.screen.name : "" })
    }
}
