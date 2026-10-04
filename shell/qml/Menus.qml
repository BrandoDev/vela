pragma Singleton

import QtQuick

// Tutti i menu del tasto destro di Vela passano da qui (docs/renderer.md
// §14): chi vuole un menu chiama Menus.open(voci, x, y, opzioni) e la
// finestra ContextMenu lo mostra. x e y sono in coordinate dello schermo.
//
// Una voce è un oggetto JS:
//   { text: "&Apri", icon: "document-open", shortcut: "Ctrl+O",
//     enabled: true, checked: true/false (solo se attivabile), radio: true,
//     action: function() {...}, children: [voci], context: [voci] }
//   { separator: true }       una riga di separazione
//   { header: "Recenti" }     un titolo di gruppo (jump list)
//   { ..., pin: { pinned: false, toggle: function() {...} } }  la puntina
// La "&" segna la lettera per la tastiera (sottolineata se aperto da tastiera).
//
// Opzioni: screen (nome dello schermo), above (il menu sale da y), centered
// (x è il centro), keyboard (aperto da tastiera), minWidth, rebuild
// (funzione che rifà le voci, es. dopo aver fissato un file), onClosed.
QtObject {
    id: menus

    // Aperto: i pannelli da cui nasce (es. il menu Start) non si chiudono
    // quando perdono la tastiera a favore del menu.
    property bool isOpen: false
    // I pannelli a destra della taskbar: si apre uno alla volta.
    property bool quickSettingsOpen: false
    property bool notificationCenterOpen: false
    // La Visualizzazione attività è aperta.
    property bool taskViewOpen: false

    signal openRequested(var entries, real x, real y, var options)
    signal closeRequested()
    // Una domanda con Sì e No (es. "Svuota Cestino"): ConfirmDialog.qml.
    signal confirmRequested(string title, string text, string yesText, var action)

    function confirm(title, text, yesText, action) {
        confirmRequested(title, text, yesText, action)
    }

    // La finestra Proprietà (PropertiesDialog.qml) per questi file.
    signal propertiesRequested(var paths)
    function showProperties(paths) {
        propertiesRequested(paths)
    }

    function open(entries, x, y, options) {
        isOpen = true
        openRequested(entries, x, y, options || {})
    }

    function close() {
        closeRequested()
    }

    // Le voci del menu di un campo di testo, come in Windows.
    function textEntries(input) {
        const selected = input.selectedText.length > 0
        const editable = !input.readOnly
        return [
            { text: "&Annulla", icon: "edit-undo", shortcut: "Ctrl+Z", enabled: editable && input.canUndo, action: () => input.undo() },
            { separator: true },
            { text: "&Taglia", icon: "edit-cut", shortcut: "Ctrl+X", enabled: editable && selected, action: () => input.cut() },
            { text: "&Copia", icon: "edit-copy", shortcut: "Ctrl+C", enabled: selected, action: () => input.copy() },
            { text: "&Incolla", icon: "edit-paste", shortcut: "Ctrl+V", enabled: editable && input.canPaste, action: () => input.paste() },
            { text: "&Elimina", icon: "edit-delete", shortcut: "Canc", enabled: editable && selected, action: () => input.remove(input.selectionStart, input.selectionEnd) },
            { separator: true },
            { text: "&Seleziona tutto", icon: "edit-select-all", shortcut: "Ctrl+A", enabled: input.text.length > 0, action: () => input.selectAll() }
        ]
    }

    // Il menu della finestra (barra del titolo, Alt+Spazio, Maiusc+clic
    // destro sul pulsante della taskbar). act(azione) la esegue.
    function windowEntries(maximized, minimized, resizable, act) {
        return [
            { text: "&Ripristina", icon: "window-restore", enabled: maximized || minimized, action: () => act("restore") },
            { text: "&Sposta", enabled: !maximized && !minimized, action: () => act("move") },
            { text: "Ri&dimensiona", enabled: !maximized && !minimized && resizable, action: () => act("resize") },
            { text: "Riduci a ic&ona", icon: "window-minimize", enabled: !minimized, action: () => act("minimize") },
            { text: "&Ingrandisci", icon: "window-maximize", enabled: !maximized && resizable, action: () => act("maximize") },
            { separator: true },
            { text: "&Chiudi", icon: "window-close", shortcut: "Alt+F4", action: () => act("close") }
        ]
    }
}
