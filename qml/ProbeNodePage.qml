pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    readonly property bool running: DhtController.running
    readonly property int labelWidth: 150
    readonly property bool nodeUsable: page.running && nodeField.text.trim().length > 0
                                       && nodeField.validation === "" && !DhtController.probeBusy
    readonly property bool hashUsable: page.nodeUsable && hashField.text.trim().length > 0
                                       && hashField.validation === ""

    contentWidth: availableWidth
    padding: Theme.spacingLarge
    clip: true

    component FieldLabel: Label {
        Layout.preferredWidth: page.labelWidth
        color: Theme.textDim
        font.pixelSize: Theme.fontSizeSmall
    }

    component Hint: Label {
        Layout.fillWidth: true
        color: Theme.textFaint
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }

    component Cell: Label {
        property int cellWidth: 0
        Layout.preferredWidth: cellWidth
        Layout.fillWidth: cellWidth === 0
        elide: Text.ElideRight
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.text
    }

    component ResultRow: RowLayout {
        id: resultRow

        property string label
        property string value
        property color tone: Theme.text

        Layout.fillWidth: true
        spacing: Theme.spacing

        FieldLabel { text: resultRow.label }

        Label {
            Layout.fillWidth: true
            text: resultRow.value
            color: resultRow.tone
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.monoFamily
            wrapMode: Text.WrapAnywhere
        }
    }

    // Verbatim output: selectable so it can be copied out of the tool.
    component Verbatim: TextEdit {
        Layout.fillWidth: true
        readOnly: true
        selectByMouse: true
        selectionColor: Theme.accent
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.WrapAnywhere
        font.pixelSize: Theme.fontSizeSmall
        font.family: Theme.monoFamily
        color: Theme.text
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingLarge

        // --- the node under examination ---------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Node")
            subtitle: qsTr("Every query on this page goes to this one node and nowhere else. Whatever comes back is decoded and shown in full, alongside the bytes on the wire.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Address") }

                ThemedTextField {
                    id: nodeField

                    readonly property string validation: DhtController.validateEndpoint(text)

                    Layout.fillWidth: true
                    Accessible.name: qsTr("Node address")
                    enabled: page.running && !DhtController.probeBusy
                    placeholderText: qsTr("address:port, e.g. 67.215.246.10:6881 or router.bittorrent.com:6881")
                    invalid: validation !== ""
                    // Held by the controller so a link on another tab can set
                    // it. textChanged rather than textEdited, so assistive
                    // tools work; echoes from the controller compare equal.
                    text: DhtController.probeAddress
                    onTextChanged: if (text !== DhtController.probeAddress) DhtController.probeAddress = text
                }

                ThemedButton {
                    text: qsTr("Ping")
                    primary: true
                    enabled: page.nodeUsable
                    onClicked: DhtController.probeNode(nodeField.text, "ping", "")
                }
            }

            Label {
                Layout.fillWidth: true
                visible: nodeField.validation !== "" || !page.running
                text: !page.running ? qsTr("Start the engine on the Setup tab to probe a node.") : nodeField.validation
                color: !page.running ? Theme.textFaint : Theme.bad
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        // --- what to ask it -----------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Queries")
            subtitle: qsTr("find_node and get take a target; get_peers and announce_peer take an infohash. announce_peer asks get_peers first, because it needs that node's token, and both exchanges are recorded.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Hash") }

                ThemedTextField {
                    id: hashField

                    readonly property string validation: DhtController.validateHash(text)

                    Layout.fillWidth: true
                    Accessible.name: qsTr("Target or infohash")
                    enabled: page.running && !DhtController.probeBusy
                    placeholderText: qsTr("40 hexadecimal digits")
                    invalid: validation !== ""
                }

                ThemedButton {
                    text: qsTr("Random")
                    Accessible.name: qsTr("Generate a random hash")
                    enabled: !DhtController.probeBusy
                    onClicked: hashField.text = DhtController.randomHash()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Queries") }

                ThemedButton {
                    text: qsTr("find_node")
                    enabled: page.hashUsable
                    onClicked: DhtController.probeNode(nodeField.text, "find_node", hashField.text)
                }

                ThemedButton {
                    text: qsTr("get_peers")
                    enabled: page.hashUsable
                    onClicked: DhtController.probeNode(nodeField.text, "get_peers", hashField.text)
                }

                ThemedButton {
                    text: qsTr("get")
                    enabled: page.hashUsable
                    onClicked: DhtController.probeNode(nodeField.text, "get", hashField.text)
                }

                ThemedButton {
                    text: qsTr("announce_peer")
                    enabled: page.hashUsable && (impliedPort.checked || announcePort.acceptableInput)
                    onClicked: DhtController.probeAnnounce(nodeField.text, hashField.text,
                                                           parseInt(announcePort.text), impliedPort.checked)
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Announce port") }

                ThemedTextField {
                    id: announcePort
                    Layout.preferredWidth: 90
                    Accessible.name: qsTr("Peer port to announce")
                    enabled: page.running && !DhtController.probeBusy && !impliedPort.checked
                    text: "6881"
                    validator: IntValidator { bottom: 1; top: 65535 }
                    invalid: !acceptableInput
                }

                ThemedSwitch {
                    id: impliedPort
                    text: qsTr("use this node's own port")
                    Accessible.name: qsTr("Announce the port this node listens on")
                    enabled: page.running && !DhtController.probeBusy
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Anything else") }

                ThemedTextField {
                    id: customMethod
                    Layout.preferredWidth: 200
                    Accessible.name: qsTr("Custom method name")
                    enabled: page.running && !DhtController.probeBusy
                    placeholderText: qsTr("e.g. frobnicate")
                }

                ThemedButton {
                    text: qsTr("Send")
                    Accessible.name: qsTr("Send custom method")
                    enabled: page.nodeUsable && customMethod.text.trim().length > 0
                    onClicked: DhtController.probeNode(nodeField.text, customMethod.text, hashField.text)
                }

                Hint { text: qsTr("A method the node does not know should come back as error 204, which is a quick check that it handles the unexpected properly.") }
            }
        }

        // --- the exchange -------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Exchange")
            subtitle: DhtController.probe.valid
                      ? qsTr("%1 to %2 at %3").arg(DhtController.probe.method)
                        .arg(DhtController.probe.endpoint).arg(DhtController.probe.time)
                      : qsTr("Whatever the node sends back appears here, decoded in full.")

            ResultRow {
                label: qsTr("Outcome")
                value: DhtController.probe.valid ? DhtController.probe.outcome : qsTr("nothing asked yet")
                tone: DhtController.probe.outcome === "ok" ? Theme.good
                      : DhtController.probe.outcome === "timeout" ? Theme.warn
                      : DhtController.probe.valid ? Theme.bad : Theme.textFaint
            }
            ResultRow { label: qsTr("Round trip"); value: DhtController.probe.rtt; visible: DhtController.probe.valid }
            ResultRow { label: qsTr("Summary"); value: DhtController.probe.summary; visible: DhtController.probe.valid }
            ResultRow {
                label: qsTr("Problem")
                value: DhtController.probe.errorMessage
                tone: Theme.bad
                visible: DhtController.probe.errorMessage !== ""
            }

            Label {
                Layout.fillWidth: true
                visible: DhtController.probe.decoded !== ""
                text: qsTr("Decoded reply")
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                topPadding: Theme.spacingSmall
            }

            Verbatim {
                visible: DhtController.probe.decoded !== ""
                text: DhtController.probe.decoded
            }

            Label {
                Layout.fillWidth: true
                visible: DhtController.probe.response !== ""
                text: qsTr("Reply on the wire")
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                topPadding: Theme.spacingSmall
            }

            Verbatim {
                visible: DhtController.probe.response !== ""
                text: DhtController.probe.response
                color: Theme.textDim
            }

            Label {
                Layout.fillWidth: true
                visible: DhtController.probe.request !== ""
                text: qsTr("What we sent")
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                topPadding: Theme.spacingSmall
            }

            Verbatim {
                visible: DhtController.probe.request !== ""
                text: DhtController.probe.request
                color: Theme.textDim
            }
        }

        // --- everything asked so far ----------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("History")
            subtitle: qsTr("Every query this session, newest first. Select one to see it again in full.")

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 28
                    color: Theme.surfaceAlt
                    radius: Theme.radius

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        Cell { cellWidth: 80; text: qsTr("Time"); color: Theme.textDim; font.weight: Font.DemiBold }
                        Cell { cellWidth: 120; text: qsTr("Method"); color: Theme.textDim; font.weight: Font.DemiBold }
                        Cell { cellWidth: 90; text: qsTr("Outcome"); color: Theme.textDim; font.weight: Font.DemiBold }
                        Cell { cellWidth: 70; text: qsTr("RTT"); color: Theme.textDim; font.weight: Font.DemiBold }
                        Cell { text: qsTr("Summary"); color: Theme.textDim; font.weight: Font.DemiBold }
                    }
                }

                ListView {
                    id: historyList

                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: DhtController.probeHistory
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: historyRow

                        required property int index
                        required property string method
                        required property string endpoint
                        required property string outcome
                        required property string rtt
                        required property string time
                        required property string summary

                        readonly property bool current: historyRow.index === DhtController.selectedProbe

                        width: ListView.view.width
                        height: 26
                        color: historyRow.current ? Qt.rgba(0.29, 0.62, 1.0, 0.18)
                               : rowHover.hovered ? Theme.surfaceAlt : "transparent"

                        HoverHandler { id: rowHover }
                        TapHandler { onTapped: DhtController.selectProbe(historyRow.index) }
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("%1 at %2, %3").arg(historyRow.method).arg(historyRow.time).arg(historyRow.outcome)
                        Accessible.onPressAction: DhtController.selectProbe(historyRow.index)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            spacing: Theme.spacingSmall

                            Cell { cellWidth: 80; text: historyRow.time; color: Theme.textDim }
                            Cell { cellWidth: 120; text: historyRow.method; font.family: Theme.monoFamily }
                            Cell {
                                cellWidth: 90
                                text: historyRow.outcome
                                color: historyRow.outcome === "ok" ? Theme.good
                                       : historyRow.outcome === "timeout" ? Theme.warn : Theme.bad
                            }
                            Cell { cellWidth: 70; text: historyRow.rtt; color: Theme.textDim }
                            Cell { text: historyRow.summary; color: Theme.textDim }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: historyList.count === 0
                        text: page.running ? qsTr("Nothing asked yet") : qsTr("Engine stopped")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                Item { Layout.fillWidth: true }

                ThemedButton {
                    text: qsTr("Clear history")
                    enabled: DhtController.probeHistory.count > 0
                    onClicked: DhtController.clearProbes()
                }
            }
        }
    }
}
