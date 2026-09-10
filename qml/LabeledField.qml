import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Read-only "label: value" row. Values are monospaced because almost
// everything this tool reports is an address, an ID, or a number.
RowLayout {
    id: root

    property string label
    property string value
    property color valueColor: Theme.text
    property int labelWidth: 170

    Layout.fillWidth: true
    spacing: Theme.spacing

    Label {
        Layout.preferredWidth: root.labelWidth
        Layout.alignment: Qt.AlignTop
        text: root.label
        color: Theme.textDim
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }

    Label {
        Layout.fillWidth: true
        text: root.value
        color: root.valueColor
        font.pixelSize: Theme.fontSizeSmall
        font.family: Theme.monoFamily
        elide: Text.ElideRight
    }
}
