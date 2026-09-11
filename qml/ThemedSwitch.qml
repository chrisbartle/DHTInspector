import QtQuick
import QtQuick.Controls

Switch {
    id: control

    spacing: Theme.spacing
    padding: 0
    font.pixelSize: Theme.fontSizeNormal

    indicator: Rectangle {
        implicitWidth: 38
        implicitHeight: 20
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.surfaceAlt
        border.color: control.checked ? Theme.accent : Theme.border
        border.width: 1
        opacity: control.enabled ? 1 : 0.45

        Rectangle {
            width: 14
            height: 14
            radius: 7
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 3 : 3
            color: Theme.text

            Behavior on x {
                NumberAnimation { duration: 110; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Label {
        text: control.text
        leftPadding: control.indicator.width + (control.text.length > 0 ? control.spacing : 0)
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? Theme.text : Theme.textFaint
        font: control.font
    }
}
