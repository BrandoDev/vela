import QtQuick

Page {
    CardGroup {
        LinkCard {
            icon: "view-list-details"
            title: "App installate"
            description: "Disinstalla e cerca le app"
            onClicked: root.navigate("installed-apps")
        }
        LinkCard {
            icon: "preferences-desktop-default-applications"
            title: "App predefinite"
            description: "Le app che aprono pagine web, posta, musica, foto e altri file"
            onClicked: root.navigate("default-apps")
        }
    }
}
