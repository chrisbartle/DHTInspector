import QtQuick
import QtQuick.Controls

// A DHT node address shown as a link. Clicking it loads the address into the
// Probe Node tab and switches to it, so anything the tool reports about a
// node can be followed straight to an interrogation of that node.
Label {
    id: root

    property string address
    readonly property bool active: root.address !== ""

    text: root.address
    color: root.active ? (hover.hovered ? Qt.lighter(Theme.accent, 1.25) : Theme.accent) : Theme.textDim
    font.family: Theme.monoFamily
    font.pixelSize: Theme.fontSizeSmall
    font.underline: hover.hovered

    Accessible.role: Accessible.Link
    Accessible.name: root.address
    Accessible.description: qsTr("Probe this node")
    Accessible.onPressAction: root.probe()

    function probe() {
        if (root.active)
            DhtController.openProbe(root.address)
    }

    // Not blocking, so a row underneath still sees the hover for its own
    // highlight and tooltip.
    HoverHandler {
        id: hover
        enabled: root.active
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.active
        onTapped: root.probe()
    }
}
