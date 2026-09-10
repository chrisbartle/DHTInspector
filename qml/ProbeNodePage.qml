import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    contentWidth: availableWidth
    padding: Theme.spacingLarge
    clip: true

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingLarge

        Panel {
            Layout.fillWidth: true
            title: qsTr("Target")
            subtitle: qsTr("A single node to interrogate, as address:port.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                TextField {
                    id: targetField

                    Layout.fillWidth: true
                    enabled: false
                    placeholderText: qsTr("e.g. 67.215.246.10:6881")
                    color: Theme.text
                    placeholderTextColor: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                    padding: Theme.spacingSmall + 2

                    background: Rectangle {
                        color: Theme.background
                        border.color: targetField.activeFocus ? Theme.accent : Theme.border
                        border.width: 1
                        radius: Theme.radius
                    }
                }

                Button {
                    id: probeButton

                    enabled: false
                    text: qsTr("Probe")
                    padding: Theme.spacingSmall + 2

                    contentItem: Label {
                        text: probeButton.text
                        color: probeButton.enabled ? Theme.text : Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        horizontalAlignment: Text.AlignHCenter
                    }

                    background: Rectangle {
                        implicitWidth: 90
                        color: probeButton.down ? Theme.surfaceAlt : Theme.surface
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radius
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: page.availableWidth > 900 ? 2 : 1
            columnSpacing: Theme.spacingLarge
            rowSpacing: Theme.spacingLarge

            Panel {
                Layout.fillWidth: true
                title: qsTr("Reachability")
                subtitle: qsTr("Round-trip time, jitter and loss under sustained querying.")
                EmptyState { message: qsTr("Not probed") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Identity")
                subtitle: qsTr("Node ID stability and BEP 42 compliance for the observed address.")
                EmptyState { message: qsTr("Not probed") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Protocol Conformance")
                subtitle: qsTr("Transaction echo, error handling, token issuance and rotation.")
                EmptyState { message: qsTr("Not probed") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Capabilities")
                subtitle: qsTr("Which extensions the node speaks: BEP 32, 33, 43, 44, 51.")
                EmptyState { message: qsTr("Not probed") }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Transcript")
            subtitle: qsTr("Every query sent and every response received, verbatim, including anything that failed to parse.")
            EmptyState {
                message: qsTr("No traffic")
                implicitHeight: 180
            }
        }

    }
}
