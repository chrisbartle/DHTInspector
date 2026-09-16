import QtQuick
import QtQuick.Controls

Slider {
    id: control

    padding: 0
    implicitWidth: 220

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 4
        width: control.availableWidth
        height: implicitHeight
        radius: 2
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        opacity: control.enabled ? 1 : 0.45

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: control.pressed ? Qt.lighter(Theme.text, 1.1) : Theme.text
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.45
    }
}
