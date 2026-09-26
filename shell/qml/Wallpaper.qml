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
}
