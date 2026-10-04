import QtQuick
import QtQuick.Window

// La finestra delle Impostazioni, come Windows 11: a sinistra l'account,
// la ricerca e le sezioni; a destra la pagina, con il percorso nel titolo.
Window {
    id: root
    width: 1100
    height: 760
    minimumWidth: 760
    minimumHeight: 500
    visible: true
    title: "Impostazioni"
    color: Theme.background

    onActiveChanged: {
        Theme.windowActive = active
        if (active) {
            Prefs.reload() // la shell può aver cambiato qualcosa (Non disturbare...)
        }
    }

    // --- le pagine ---
    readonly property var pages: ({
        "home": { title: "Home", parent: "", file: "HomePage.qml", icon: "go-home" },
        "system": { title: "Sistema", parent: "", file: "SystemPage.qml", icon: "computer" },
        "display": { title: "Schermo", parent: "system", file: "DisplayPage.qml", icon: "video-display" },
        "sound": { title: "Audio", parent: "system", file: "SoundPage.qml", icon: "audio-speakers" },
        "notifications": { title: "Notifiche", parent: "system", file: "NotificationsPage.qml", icon: "preferences-desktop-notification" },
        "power": { title: "Alimentazione", parent: "system", file: "PowerPage.qml", icon: "preferences-system-power-management" },
        "about": { title: "Informazioni", parent: "system", file: "AboutPage.qml", icon: "help-about" },
        "bluetooth": { title: "Bluetooth e dispositivi", parent: "", file: "BluetoothPage.qml", icon: "preferences-system-bluetooth" },
        "network": { title: "Rete e Internet", parent: "", file: "NetworkPage.qml", icon: "preferences-system-network" },
        "personalization": { title: "Personalizzazione", parent: "", file: "PersonalizationPage.qml", icon: "preferences-desktop-theme" },
        "background": { title: "Sfondo", parent: "personalization", file: "BackgroundPage.qml", icon: "preferences-desktop-wallpaper" },
        "colors": { title: "Colori", parent: "personalization", file: "ColorsPage.qml", icon: "preferences-desktop-color" },
        "taskbar": { title: "Barra delle applicazioni", parent: "personalization", file: "TaskbarPage.qml", icon: "preferences-system-windows" },
        "apps": { title: "App", parent: "", file: "AppsPage.qml", icon: "preferences-desktop-default-applications" },
        "installed-apps": { title: "App installate", parent: "apps", file: "InstalledAppsPage.qml", icon: "view-list-details" },
        "default-apps": { title: "App predefinite", parent: "apps", file: "DefaultAppsPage.qml", icon: "preferences-desktop-default-applications" },
        "time-language": { title: "Ora e lingua", parent: "", file: "TimeLanguagePage.qml", icon: "preferences-system-time" },
        "datetime": { title: "Data e ora", parent: "time-language", file: "DateTimePage.qml", icon: "preferences-system-time" },
        "keyboard": { title: "Tastiera", parent: "time-language", file: "KeyboardPage.qml", icon: "input-keyboard" },
        "night-light": { title: "Luce notturna", parent: "display", file: "NightLightPage.qml", icon: "redshift-status-on" },
        "accessibility": { title: "Accessibilità", parent: "", file: "AccessibilityPage.qml", icon: "preferences-desktop-accessibility" }
    })
    readonly property var sections: ["home", "system", "bluetooth", "network", "personalization", "apps", "time-language", "accessibility"]
    // I nomi che usa la shell (systemactions.cpp) per le voci dei menu.
    readonly property var aliases: ({
        "settings": "home", "personalize": "personalization", "taskbar-settings": "taskbar",
        "notification-settings": "notifications", "sound-settings": "sound", "devices": "bluetooth",
        "mobility": "power", "computer": "about", "installed-apps": "installed-apps",
        "bluetooth-settings": "bluetooth", "night-light-settings": "night-light", "accessibility-settings": "accessibility",
        "colors-settings": "colors"
    })

    property string current: "home"
    readonly property string section: {
        let name = current
        while (pages[name] && pages[name].parent !== "") {
            name = pages[name].parent
        }
        return name
    }
    readonly property var crumbs: {
        const list = []
        let name = current
        while (name !== "" && pages[name]) {
            list.unshift({ name: name, title: pages[name].title })
            name = pages[name].parent
        }
        // Home non fa da genitore nel percorso, come su Windows.
        return list
    }

    function navigate(name) {
        name = aliases[name] || name
        if (!pages[name]) {
            name = "home"
        }
        current = name
        search.text = ""
    }

    Component.onCompleted: navigate(Router.initialPage)

    Connections {
        target: Router
        function onPageRequested(page) {
            if (page !== "") {
                root.navigate(page)
            }
            if (root.visibility === Window.Minimized) {
                root.showNormal()
            }
            root.requestActivate()
        }
    }

    // --- la ricerca: "Trova un'impostazione" ---
    readonly property var searchIndex: [
        { page: "display", text: "Schermo", keys: "risoluzione scala frequenza aggiornamento hz orientamento monitor più schermi disposizione" },
        { page: "display", text: "Modifica la risoluzione dello schermo", keys: "risoluzione" },
        { page: "display", text: "Modifica la scala", keys: "scala dimensione testo dpi zoom" },
        { page: "display", text: "Frequenza di aggiornamento", keys: "hz refresh frequenza" },
        { page: "sound", text: "Audio", keys: "volume altoparlanti cuffie microfono uscita ingresso suono" },
        { page: "sound", text: "Scegli il dispositivo di output", keys: "uscita altoparlanti cuffie" },
        { page: "sound", text: "Microfono", keys: "ingresso input registrazione" },
        { page: "notifications", text: "Notifiche", keys: "non disturbare avvisi" },
        { page: "notifications", text: "Non disturbare", keys: "silenzioso notifiche" },
        { page: "power", text: "Alimentazione", keys: "schermo spento sospensione inattività batteria risparmio energia blocco" },
        { page: "power", text: "Spegni lo schermo dopo", keys: "inattività timeout" },
        { page: "power", text: "Modalità di risparmio energia", keys: "profilo prestazioni bilanciato" },
        { page: "about", text: "Informazioni", keys: "specifiche processore ram memoria nome pc rinomina versione kernel scheda video" },
        { page: "about", text: "Rinomina questo PC", keys: "nome dispositivo hostname" },
        { page: "bluetooth", text: "Bluetooth", keys: "dispositivi cuffie mouse tastiera associa aggiungi" },
        { page: "bluetooth", text: "Aggiungi dispositivo", keys: "associa bluetooth nuovo" },
        { page: "network", text: "Rete e Internet", keys: "wifi wi-fi ethernet cavo ip dns connessione" },
        { page: "network", text: "Wi-Fi", keys: "wireless rete senza fili password" },
        { page: "personalization", text: "Personalizzazione", keys: "tema aspetto" },
        { page: "background", text: "Sfondo", keys: "immagine desktop foto wallpaper" },
        { page: "colors", text: "Colori", keys: "accento colore principale tema scuro chiaro modalità" },
        { page: "colors", text: "Scegli la modalità", keys: "scuro chiaro dark light tema app" },
        { page: "taskbar", text: "Barra delle applicazioni", keys: "taskbar allineamento sinistra centro termina attività" },
        { page: "apps", text: "App", keys: "programmi applicazioni" },
        { page: "installed-apps", text: "App installate", keys: "disinstalla rimuovi programmi" },
        { page: "default-apps", text: "App predefinite", keys: "browser posta lettore apri con predefinito" },
        { page: "datetime", text: "Data e ora", keys: "orologio fuso orario sincronizza ntp" },
        { page: "keyboard", text: "Tastiera", keys: "layout lingua input digitazione ripetizione tasti" },
        { page: "night-light", text: "Luce notturna", keys: "notte colori caldi blu sera pianifica tramonto" },
        { page: "display", text: "Tearing nei giochi", keys: "giochi tearing latenza schermo intero vsync" },
        { page: "accessibility", text: "Accessibilità", keys: "ipovedenti daltonismo" },
        { page: "accessibility", text: "Lente di ingrandimento", keys: "zoom ingrandire magnifier" },
        { page: "accessibility", text: "Filtri colore", keys: "scala di grigi daltonismo deuteranopia protanopia tritanopia" },
        { page: "accessibility", text: "Tasti permanenti", keys: "sticky keys maiusc ctrl alt tastiera" }
    ]
    readonly property var searchResults: {
        const q = search.text.trim().toLowerCase()
        if (q === "") {
            return []
        }
        return searchIndex.filter(e => e.text.toLowerCase().indexOf(q) >= 0 || e.keys.indexOf(q) >= 0).slice(0, 8)
    }

    // --- navigazione a sinistra ---
    Item {
        id: nav
        x: 0
        y: 0
        width: 296
        height: parent.height

        // L'account
        Row {
            id: account
            x: 16
            y: 16
            spacing: 14
            Rectangle {
                width: 64
                height: 64
                radius: 32
                color: Theme.accentFill
                Text {
                    anchors.centerIn: parent
                    text: About.userName.charAt(0).toUpperCase()
                    color: Theme.accentText
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: nav.width - 32 - 64 - 14
                Text {
                    width: parent.width
                    text: About.userName
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    text: "Account locale"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        TextBox {
            id: search
            x: 16
            anchors.top: account.bottom
            anchors.topMargin: 20
            width: nav.width - 32
            placeholderText: "Trova un'impostazione"
            rightPadding: 36
            Keys.onReturnPressed: {
                if (root.searchResults.length > 0) {
                    root.navigate(root.searchResults[0].page)
                }
            }
            Keys.onEscapePressed: text = ""
            Image {
                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                width: 16
                height: 16
                source: Theme.icons + "edit-find"
                sourceSize: Qt.size(width, height)
                opacity: 0.8
            }
        }

        ListView {
            id: sectionList
            anchors { top: search.bottom; topMargin: 12; left: parent.left; right: parent.right; bottom: parent.bottom }
            leftMargin: 8
            rightMargin: 8
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            model: root.sections
            spacing: 2
            delegate: Rectangle {
                id: entry
                required property string modelData
                readonly property bool selected: root.section === modelData
                width: sectionList.width - 16
                height: 36
                radius: Theme.radius
                color: entryMouse.pressed ? Theme.subtlePressed : selected || entryMouse.containsMouse ? Theme.subtleHover : "transparent"

                Rectangle {
                    visible: entry.selected
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: entryMouse.pressed ? 10 : 16
                    radius: 1.5
                    color: Theme.accentFill
                }
                Image {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    source: "image://fileicon/" + encodeURIComponent(root.pages[entry.modelData].icon)
                    sourceSize: Qt.size(width, height)
                }
                Text {
                    x: 48
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.pages[entry.modelData].title
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                }
                MouseArea {
                    id: entryMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.navigate(entry.modelData)
                }
            }
        }

        // I risultati della ricerca, sotto la casella.
        Rectangle {
            visible: root.searchResults.length > 0 && search.activeFocus
            x: search.x
            anchors.top: search.bottom
            anchors.topMargin: 4
            width: search.width
            height: results.height + 8
            radius: Theme.radiusOverlay
            color: Theme.flyout
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.4)
            z: 10
            Column {
                id: results
                x: 4
                y: 4
                width: parent.width - 8
                Repeater {
                    model: root.searchResults
                    delegate: Rectangle {
                        id: result
                        required property var modelData
                        width: results.width
                        height: 40
                        radius: Theme.radius
                        color: resultMouse.containsMouse ? Theme.subtleHover : "transparent"
                        Image {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            source: "image://fileicon/" + encodeURIComponent(root.pages[result.modelData.page].icon)
                            sourceSize: Qt.size(width, height)
                        }
                        Text {
                            x: 38
                            width: parent.width - 46
                            anchors.verticalCenter: parent.verticalCenter
                            text: result.modelData.text
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: resultMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.navigate(result.modelData.page)
                        }
                    }
                }
            }
        }
    }

    // --- la pagina ---
    Item {
        id: content
        anchors { left: nav.right; top: parent.top; bottom: parent.bottom; right: parent.right; leftMargin: 20 }

        PageHeader {
            id: header
            y: 20
            crumbs: root.crumbs
            onNavigate: name => root.navigate(name)
        }

        Loader {
            id: pageLoader
            anchors { top: header.bottom; topMargin: 20; left: parent.left; right: parent.right; bottom: parent.bottom }
            source: Qt.resolvedUrl(root.pages[root.current].file)
            onLoaded: {
                item.opacity = 0
                slide.y = 24
                enter.restart()
            }
            transform: Translate { id: slide }
            ParallelAnimation {
                id: enter
                NumberAnimation { target: pageLoader.item; property: "opacity"; to: 1; duration: Theme.normal }
                NumberAnimation { target: slide; property: "y"; to: 0; duration: 300; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.decelerate }
            }
        }
    }
}
