import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Text with a copy button right beside it, for table cells. The text keeps
// its natural width, so the button sits against it instead of at the far
// edge of the column, and a spacer takes whatever width is left over. In a
// column narrower than the text, the text elides and the button stays.
// value can differ from text: a shortened ID shown, the whole one copied.
RowLayout {
    id: root

    property string text
    property string value: root.text
    property string what
    property color color: Theme.text
    property bool mono: true
    property int elide: Text.ElideRight
    property string toolTip
    readonly property alias copyButton: button

    // A layout nested in another one fills the spare width unless told
    // otherwise, which would push the columns after it out of line with
    // their headers. This one takes the width its column gives it.
    Layout.fillWidth: false
    spacing: 2

    Label {
        text: root.text
        color: root.color
        elide: root.elide
        font.pixelSize: Theme.fontSizeSmall
        font.family: root.mono ? Theme.monoFamily : Qt.application.font.family

        ToolTip.visible: root.toolTip !== "" && hover.hovered
        ToolTip.delay: 500
        ToolTip.text: root.toolTip
        HoverHandler { id: hover }
    }

    CopyButton {
        id: button
        visible: root.value !== ""
        value: root.value
        what: root.what
    }

    Item { Layout.fillWidth: true }
}
