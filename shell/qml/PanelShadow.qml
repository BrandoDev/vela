import QtQuick
import QtQuick.Effects

// L'ombra morbida di un pannello, disegnata solo *fuori* dal pannello: i
// pannelli acrylic sono semitrasparenti e sotto devono mostrare lo sfondo
// sfocato, non un'ombra. RectangularShadow (analitica, come quella del
// compositor) ritagliata in quattro strisce attorno al pannello.
Item {
    id: root

    required property Item target
    property real radius: Theme.radiusLarge
    property real blur: 32
    property vector2d offset: Qt.vector2d(0, 6)
    property color color: Qt.rgba(0, 0, 0, 0.45)
    readonly property real reach: blur + Math.max(Math.abs(offset.x), Math.abs(offset.y)) + 8

    anchors.fill: target
    anchors.margins: -reach

    component Strip: Item {
        clip: true
        RectangularShadow {
            x: root.reach - parent.x
            y: root.reach - parent.y
            width: root.target.width
            height: root.target.height
            radius: root.radius
            blur: root.blur
            offset: root.offset
            color: root.color
        }
    }

    Strip { x: 0; y: 0; width: root.width; height: root.reach }
    Strip { x: 0; y: root.reach + root.target.height; width: root.width; height: root.reach }
    Strip { x: 0; y: root.reach; width: root.reach; height: root.target.height }
    Strip { x: root.reach + root.target.width; y: root.reach; width: root.reach; height: root.target.height }
}
