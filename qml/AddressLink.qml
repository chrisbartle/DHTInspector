import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// An address, with a button beside it that copies it. As a link, the
// default, clicking the address loads it into the Probe Node tab and
// switches there, so anything the tool reports about a node can be followed
// straight to an interrogation of that node. Addresses that are not DHT
// nodes, such as the BitTorrent peers a search returns, set probeable to
// false and show as plain text.
RowLayout {
    id: root

    property string address
    property bool probeable: true
    property int elide: Text.ElideNone
    readonly property bool active: root.address !== ""
    readonly property bool linked: root.active && root.probeable

    spacing: 2

    function probe() {
        if (root.linked)
            DhtController.openProbe(root.address)
    }

    Label {
        Layout.fillWidth: true
        text: root.address
        elide: root.elide
        color: root.linked ? (hover.hovered ? Qt.lighter(Theme.accent, 1.25) : Theme.accent)
                           : root.active ? Theme.text : Theme.textDim
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fontSizeSmall
        font.underline: root.linked && hover.hovered

        Accessible.role: root.linked ? Accessible.Link : Accessible.StaticText
        Accessible.name: root.address
        Accessible.description: root.linked ? qsTr("Probe this node") : ""
        Accessible.onPressAction: root.probe()

        // Not blocking, so a row underneath still sees the hover for its own
        // highlight and tooltip.
        HoverHandler {
            id: hover
            enabled: root.linked
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: root.linked
            onTapped: root.probe()
        }
    }

    CopyButton {
        visible: root.active
        value: root.address
        what: qsTr("address")
    }
}
