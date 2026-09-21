import QtQuick
import QtQuick.Controls

// A small button that puts `value` on the clipboard. It sits beside a hash
// rather than making the hash itself clickable, because several hashes live
// in table rows that already do something when clicked.
AbstractButton {
    id: control

    property string value
    // What gets copied, for the tooltip and screen readers: "infohash",
    // "node ID", "target".
    property string what: qsTr("value")
    property bool copied: false

    implicitWidth: 20
    implicitHeight: 20
    padding: 0
    hoverEnabled: true
    enabled: control.value !== ""

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Copy %1").arg(control.what)

    ToolTip.visible: control.hovered || control.copied
    ToolTip.delay: control.copied ? 0 : 500
    ToolTip.text: control.copied ? qsTr("Copied") : qsTr("Copy %1").arg(control.what)

    onClicked: {
        DhtController.copyToClipboard(control.value)
        control.copied = true
        resetTimer.restart()
    }

    Timer {
        id: resetTimer
        interval: 1200
        onTriggered: control.copied = false
    }

    // Two overlapping outlined squares, the usual copy glyph, drawn rather
    // than taken from a font so it looks the same on every platform. It
    // turns green for a moment once the value is on the clipboard.
    contentItem: Item {
        id: glyph

        readonly property color ink: !control.enabled ? Theme.textFaint
                                     : control.copied ? Theme.good
                                     : control.hovered ? Theme.accent : Theme.textDim

        Rectangle {
            x: 7; y: 4
            width: 9; height: 10
            radius: 1.5
            color: "transparent"
            border.width: 1.2
            border.color: glyph.ink
        }
        Rectangle {
            x: 4; y: 7
            width: 9; height: 10
            radius: 1.5
            color: "transparent"
            border.width: 1.2
            border.color: glyph.ink
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.background : control.hovered ? Theme.surfaceAlt : "transparent"
    }
}
