import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Stand-in for a view that has no data yet. Every one of these marks a
// place where a real model gets wired in later.
Rectangle {
    id: root

    property string message: qsTr("No data")

    Layout.fillWidth: true
    implicitHeight: 96

    color: Theme.background
    border.color: Theme.border
    border.width: 1
    radius: Theme.radius

    Label {
        anchors.centerIn: parent
        text: root.message
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
    }
}
