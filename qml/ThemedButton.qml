import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool primary: false

    padding: 8
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: Theme.fontSizeSmall

    contentItem: Label {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textFaint : control.primary ? "#ffffff" : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 80
        implicitHeight: 32
        radius: Theme.radius
        border.width: 1
        border.color: control.primary ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.5
        color: control.primary
               ? (control.down ? Qt.darker(Theme.accent, 1.25) : control.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
               : (control.down ? Theme.background : control.hovered ? Theme.surfaceAlt : Theme.surface)
    }
}
