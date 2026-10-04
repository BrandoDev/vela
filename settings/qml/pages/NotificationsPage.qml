import QtQuick

// Sistema > Notifiche.
Page {
    CardGroup {
        Card {
            icon: "preferences-desktop-notification"
            title: "Notifiche"
            description: "Avvisi dalle app e dal sistema, nell'angolo in basso a destra e nel centro notifiche (Win+N)"
        }
        Card {
            icon: "notifications-disabled"
            title: "Non disturbare"
            description: "Le notifiche vanno direttamente nel centro notifiche, senza comparire"
            trailing: Toggle {
                checked: Prefs.doNotDisturb
                onToggled: on => Prefs.doNotDisturb = on
            }
        }
    }
}
