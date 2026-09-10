import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A titled card. Children are laid out vertically inside it.
Rectangle {
    id: panel

    property string title
    property string subtitle
    default property alias content: contentColumn.data

    color: Theme.surface
    border.color: Theme.border
    border.width: 1
    radius: Theme.radius

    implicitWidth: layout.implicitWidth + Theme.spacingLarge * 2
    implicitHeight: layout.implicitHeight + Theme.spacingLarge * 2

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            text: panel.title
            color: Theme.text
            font.pixelSize: Theme.fontSizeNormal
            font.weight: Font.DemiBold
            visible: panel.title !== ""
        }

        Label {
            Layout.fillWidth: true
            text: panel.subtitle
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
            visible: panel.subtitle !== ""
        }

        ColumnLayout {
            id: contentColumn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing
        }
    }
}
