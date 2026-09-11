import QtQuick
import QtQuick.Controls

// Small tinted pill for a state label.
Rectangle {
    id: root

    property string text
    property color tone: Theme.textDim

    implicitWidth: label.implicitWidth + 16
    implicitHeight: label.implicitHeight + 6
    radius: height / 2
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.14)
    border.width: 1
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.45)

    Label {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.tone
        font.pixelSize: Theme.fontSizeSmall - 1
        font.weight: Font.DemiBold
    }
}
