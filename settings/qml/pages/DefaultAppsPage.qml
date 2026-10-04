import QtQuick

// App > App predefinite: per ogni uso, l'app che lo apre.
Page {
    id: page

    Text {
        width: parent.width
        text: "Scegli le app che aprono le pagine web, la posta, la musica, i video, le foto e gli altri file."
        color: Theme.textSecondary
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
        bottomPadding: 8
    }

    Repeater {
        model: DefaultApps.categories
        delegate: Card {
            id: category
            required property var modelData
            readonly property var candidates: DefaultApps.candidates(modelData.key)
            icon: modelData.current.icon || modelData.icon
            title: modelData.label
            description: modelData.current.name || "Nessuna app scelta"
            trailing: Choice {
                usable: category.candidates.length > 0
                model: category.candidates.map(a => ({ text: a.name }))
                currentIndex: category.candidates.findIndex(a => a.id === category.modelData.current.id)
                displayText: currentIndex >= 0 ? category.candidates[currentIndex].name : "Scegli un'app"
                onChosen: index => DefaultApps.setDefault(category.modelData.key, category.candidates[index].id)
            }
        }
    }
}
