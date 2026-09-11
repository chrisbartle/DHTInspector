pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 1120
    height: 740
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: qsTr("DHT Inspector")
    color: Theme.background

    header: Rectangle {
        implicitHeight: 52
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLarge
            anchors.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingLarge

            Label {
                text: qsTr("DHT Inspector")
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
            }

            TabBar {
                id: tabBar

                Layout.fillHeight: true
                Layout.fillWidth: true
                background: null

                Repeater {
                    model: [qsTr("Setup"), qsTr("Global Health"), qsTr("Probe Node")]

                    delegate: TabButton {
                        id: tabButton

                        required property string modelData

                        text: tabButton.modelData
                        width: Math.max(120, tabButton.implicitContentWidth + Theme.spacingLarge * 2)

                        contentItem: Label {
                            text: tabButton.text
                            color: tabButton.checked ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontSizeNormal
                            font.weight: tabButton.checked ? Font.DemiBold : Font.Normal
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.surfaceAlt
                                opacity: tabButton.hovered && !tabButton.checked ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 90 } }
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 2
                                color: Theme.accent
                                visible: tabButton.checked
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: tabBar.currentIndex

        SetupPage {}
        GlobalHealthPage {}
        ProbeNodePage {}
    }

    footer: Rectangle {
        implicitHeight: 30
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLarge
            anchors.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingSmall

            Rectangle {
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: DhtController.running ? Theme.good : DhtController.lastError !== "" ? Theme.bad : Theme.textFaint
            }

            Label {
                text: DhtController.running
                      ? qsTr("%1 · %2 nodes").arg(DhtController.statusText).arg(DhtController.ipv4.nodeCount + DhtController.ipv6.nodeCount)
                      : DhtController.statusText
                elide: Text.ElideRight
                Layout.maximumWidth: root.width * 0.7
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { Layout.fillWidth: true }

            Label {
                text: qsTr("v%1").arg(Qt.application.version)
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }
}
