import QtQuick
import QtQuick.Controls

// A drop-down in the app's colours. Models are lists of { label, value }.
ComboBox {
    id: control

    textRole: "label"
    valueRole: "value"
    font.pixelSize: Theme.fontSizeSmall
    leftPadding: 10
    rightPadding: 28

    delegate: ItemDelegate {
        id: option

        required property int index
        required property var modelData

        width: ListView.view ? ListView.view.width : implicitWidth
        text: modelData[control.textRole]
        highlighted: control.highlightedIndex === index
        font: control.font

        contentItem: Label {
            text: option.text
            font: option.font
            color: option.highlighted ? "#ffffff" : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: option.highlighted ? Theme.accent : "transparent"
        }
    }

    indicator: Label {
        x: control.width - width - 10
        y: (control.height - height) / 2
        text: String.fromCharCode(0x25BE)
        color: control.enabled ? Theme.textDim : Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
    }

    contentItem: Label {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.textFaint
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 32
        radius: Theme.radius
        color: Theme.background
        border.width: 1
        border.color: control.visualFocus || control.popup.visible ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.5
    }

    popup: Popup {
        y: control.height + 2
        width: Math.max(control.width, 220)
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius
        }
    }
}
