import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property bool invalid: false

    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: "#ffffff"
    font.pixelSize: Theme.fontSizeSmall
    font.family: Theme.monoFamily
    padding: 8

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 32
        radius: Theme.radius
        color: Theme.background
        border.width: 1
        border.color: control.invalid ? Theme.bad : control.activeFocus ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.5
    }
}
