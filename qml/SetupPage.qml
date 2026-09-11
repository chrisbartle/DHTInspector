pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    readonly property bool running: DhtController.running
    readonly property int labelWidth: 170

    // Column widths for the node table; 0 means "take the remaining space".
    readonly property var columns: [
        { title: qsTr("Address"), width: 0 },
        { title: qsTr("Node ID"), width: 150 },
        { title: qsTr("Status"), width: 96 },
        { title: qsTr("RTT"), width: 64 },
        { title: qsTr("Seen"), width: 52 },
        { title: qsTr("BEP 42"), width: 104 },
        { title: qsTr("Client"), width: 72 },
        { title: qsTr("Source"), width: 76 }
    ]

    function formatBytes(n) {
        if (n < 1024)
            return n + " B"
        if (n < 1024 * 1024)
            return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024)
            return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
    }

    function mappingTone(state) {
        switch (state) {
        case "mapped": return Theme.good
        case "failed": return Theme.bad
        case "discovering": return Theme.accent
        default: return Theme.textFaint
        }
    }

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

    // Always shows the controller's real state. A flip is sent as a request
    // and the binding is restored, so a refused or failed change snaps back.
    // Reacts to checkedChanged rather than toggled so that accessibility
    // tools, which change `checked` without emitting toggled, work too.
    component ControlledSwitch: ThemedSwitch {
        id: controlled

        property bool value
        signal requested(bool on)

        checked: value
        onCheckedChanged: {
            if (checked !== value) {
                requested(checked)
                checked = Qt.binding(() => controlled.value)
            }
        }
    }

    component IdentityBlock: ColumnLayout {
        id: block

        property string family
        property var status
        property bool familyEnabled: true

        readonly property bool live: page.running && block.familyEnabled && block.status.bound

        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Label {
                text: block.family
                color: Theme.text
                font.pixelSize: Theme.fontSizeNormal
                font.weight: Font.DemiBold
                Layout.preferredWidth: page.labelWidth
            }

            Badge {
                visible: block.live
                text: Theme.bep42Text(block.status.bep42)
                tone: Theme.bep42Color(block.status.bep42)
            }

            Item { Layout.fillWidth: true }

            Label {
                visible: block.live
                text: qsTr("%1 nodes in %2 buckets").arg(block.status.nodeCount).arg(block.status.bucketCount)
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            FieldLabel { text: qsTr("Node ID") }

            TextEdit {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                selectionColor: Theme.accent
                wrapMode: TextEdit.WrapAnywhere
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.monoFamily
                color: block.live ? Theme.text : block.status.error !== "" ? Theme.bad : Theme.textFaint
                text: block.live ? block.status.nodeId
                      : !block.familyEnabled ? qsTr("— %1 disabled —").arg(block.family)
                      : block.status.error !== "" ? block.status.error
                      : qsTr("— engine stopped —")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            visible: block.live

            FieldLabel { text: qsTr("External address") }

            Label {
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSizeSmall
                font.family: Theme.monoFamily
                color: block.status.externalAddress !== "" ? Theme.text : Theme.textFaint
                text: block.status.externalAddress !== "" ? block.status.externalAddress
                                                          : qsTr("not yet agreed by other nodes")
            }
        }
    }

    component StatTile: ColumnLayout {
        property string label
        property string value

        Layout.fillWidth: true
        spacing: 2

        Label {
            text: parent.label
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
        }
        Label {
            text: parent.value
            color: Theme.text
            font.pixelSize: Theme.fontSizeNormal
            font.family: Theme.monoFamily
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingLarge

        // --- Engine ----------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Engine")
            subtitle: qsTr("Stopping the engine discards everything it holds: routing tables, stored peers, tokens and node IDs.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                ControlledSwitch {
                    text: qsTr("DHT engine")
                    value: DhtController.running
                    onRequested: on => DhtController.running = on
                }

                Item { Layout.fillWidth: true }

                Badge {
                    text: DhtController.running ? qsTr("running") : qsTr("stopped")
                    tone: DhtController.running ? Theme.good
                          : DhtController.lastError !== "" ? Theme.bad : Theme.textFaint
                }
            }

            Label {
                Layout.fillWidth: true
                text: DhtController.statusText
                color: !DhtController.running && DhtController.lastError !== "" ? Theme.bad : Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Listen port") }

                ThemedTextField {
                    id: portField
                    Layout.preferredWidth: 110
                    enabled: !page.running
                    text: String(DhtController.port)
                    validator: IntValidator { bottom: 1; top: 65535 }
                    invalid: !acceptableInput
                    onEditingFinished: DhtController.port = parseInt(text)
                    onActiveFocusChanged: if (!activeFocus && !acceptableInput) text = String(DhtController.port)
                }

                Hint { text: page.running ? qsTr("Stop the engine to change the port") : qsTr("UDP, used for both address families") }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("IPv6") }

                ControlledSwitch {
                    enabled: !page.running
                    value: DhtController.ipv6Enabled
                    onRequested: on => DhtController.ipv6Enabled = on
                }

                Hint {
                    text: DhtController.ipv6.error !== "" ? DhtController.ipv6.error
                          : page.running ? qsTr("Stop the engine to change")
                          : qsTr("Runs a second node on IPv6 (BEP 32)")
                    color: DhtController.ipv6.error !== "" ? Theme.bad : Theme.textFaint
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Port forwarding") }

                ControlledSwitch {
                    value: DhtController.portForwarding
                    onRequested: on => DhtController.portForwarding = on
                }

                Badge {
                    visible: page.running && DhtController.portForwarding
                    text: DhtController.portMapping.state
                    tone: page.mappingTone(DhtController.portMapping.state)
                }

                Hint {
                    color: DhtController.portMapping.state === "failed" ? Theme.bad : Theme.textFaint
                    text: {
                        if (!DhtController.portForwarding)
                            return qsTr("Asks the gateway to forward the port (PCP, falling back to NAT-PMP)")
                        if (!page.running)
                            return qsTr("Mapping starts with the engine")
                        const m = DhtController.portMapping
                        if (m.state === "mapped" && m.externalAddress !== "")
                            return qsTr("%1:%2 via %3").arg(m.externalAddress).arg(m.externalPort).arg(m.protocol)
                        return m.message
                    }
                }
            }
        }

        // --- Identity --------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Node Identity")
            subtitle: qsTr("Each address family runs its own node with its own ID. Once enough nodes agree on our external address, the engine adopts a BEP 42 compliant ID for it and rejoins.")

            IdentityBlock {
                family: qsTr("IPv4")
                status: DhtController.ipv4
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            IdentityBlock {
                family: qsTr("IPv6")
                status: DhtController.ipv6
                familyEnabled: DhtController.ipv6Enabled
            }
        }

        // --- Bootstrap -------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Bootstrap")
            subtitle: qsTr("The engine never joins the network on its own. Add a node you know, or contact the well-known bootstrap routers.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                ThemedTextField {
                    id: nodeField

                    readonly property string validation: {
                        DhtController.ipv6Enabled // re-validate when IPv6 is toggled
                        return DhtController.validateEndpoint(text)
                    }

                    Layout.fillWidth: true
                    enabled: page.running
                    placeholderText: qsTr("address:port, e.g. 67.215.246.10:6881, [2001:db8::1]:6881 or host.example:6881")
                    invalid: validation !== ""
                    onAccepted: if (addButton.enabled) addButton.clicked()
                }

                ThemedButton {
                    id: addButton
                    text: qsTr("Add node")
                    primary: true
                    enabled: page.running && nodeField.text.trim().length > 0 && nodeField.validation === ""
                    onClicked: {
                        if (DhtController.injectNode(nodeField.text))
                            nodeField.clear()
                    }
                }
            }

            Label {
                visible: nodeField.validation !== ""
                text: nodeField.validation
                color: Theme.bad
                font.pixelSize: Theme.fontSizeSmall
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                ThemedButton {
                    text: qsTr("Auto-bootstrap")
                    enabled: page.running
                    onClicked: DhtController.autoBootstrap()
                }

                Hint {
                    text: DhtController.bootstrapRouters().join("   ")
                    font.family: Theme.monoFamily
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }
            }

            Label {
                Layout.fillWidth: true
                visible: DhtController.notice !== ""
                text: DhtController.notice
                color: DhtController.noticeIsError ? Theme.bad : Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        // --- Nodes -----------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Nodes")
            subtitle: page.running
                      ? qsTr("%1 entries: routing table nodes plus bootstrap and injected endpoints not in the table.").arg(DhtController.nodes.count)
                      : qsTr("Routing table contents appear here while the engine runs.")

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

                        Repeater {
                            model: page.columns
                            delegate: Cell {
                                required property var modelData
                                cellWidth: modelData.width
                                text: modelData.title
                                color: Theme.textDim
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                ListView {
                    id: nodeList

                    Layout.fillWidth: true
                    Layout.preferredHeight: 380
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: DhtController.nodes
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: row

                        required property int index
                        required property string address
                        required property string nodeId
                        required property string nodeIdShort
                        required property string status
                        required property int rtt
                        required property string lastSeen
                        required property string bep42
                        required property string client
                        required property string source

                        width: ListView.view.width
                        height: 26
                        color: hover.hovered ? Theme.surfaceAlt : index % 2 === 0 ? "transparent" : Qt.rgba(1, 1, 1, 0.02)

                        HoverHandler { id: hover }
                        ToolTip.visible: hover.hovered && row.nodeId !== ""
                        ToolTip.delay: 600
                        ToolTip.text: row.nodeId

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            spacing: Theme.spacingSmall

                            Cell { cellWidth: page.columns[0].width; text: row.address; font.family: Theme.monoFamily; elide: Text.ElideMiddle }
                            Cell { cellWidth: page.columns[1].width; text: row.nodeIdShort; font.family: Theme.monoFamily; color: Theme.textDim }
                            Cell { cellWidth: page.columns[2].width; text: row.status; color: Theme.nodeStatusColor(row.status) }
                            Cell { cellWidth: page.columns[3].width; text: row.rtt >= 0 ? qsTr("%1 ms").arg(row.rtt) : "—"; color: Theme.textDim }
                            Cell { cellWidth: page.columns[4].width; text: row.lastSeen; color: Theme.textDim }
                            Cell { cellWidth: page.columns[5].width; text: row.bep42; color: Theme.bep42Color(row.bep42) }
                            Cell { cellWidth: page.columns[6].width; text: row.client; font.family: Theme.monoFamily; color: Theme.textDim }
                            Cell { cellWidth: page.columns[7].width; text: row.source; color: row.source === "routing" ? Theme.textDim : Theme.accent }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: nodeList.count === 0
                        text: page.running ? qsTr("No nodes yet. Add one or use auto-bootstrap.") : qsTr("Engine stopped")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        // --- Traffic ---------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Traffic")
            subtitle: qsTr("Totals since the engine started, both address families combined.")

            GridLayout {
                Layout.fillWidth: true
                columns: page.availableWidth > 900 ? 5 : 3
                columnSpacing: Theme.spacingLarge
                rowSpacing: Theme.spacing

                StatTile { label: qsTr("Packets in / out"); value: DhtController.stats.packetsIn + " / " + DhtController.stats.packetsOut }
                StatTile { label: qsTr("Bytes in / out"); value: page.formatBytes(DhtController.stats.bytesIn) + " / " + page.formatBytes(DhtController.stats.bytesOut) }
                StatTile { label: qsTr("Queries in / out"); value: DhtController.stats.queriesIn + " / " + DhtController.stats.queriesOut }
                StatTile { label: qsTr("Responses received"); value: String(DhtController.stats.responsesIn) }
                StatTile { label: qsTr("Timeouts"); value: String(DhtController.stats.timeouts) }
                StatTile { label: qsTr("Errors received"); value: String(DhtController.stats.errorsIn) }
                StatTile { label: qsTr("Malformed received"); value: String(DhtController.stats.malformedIn) }
                StatTile { label: qsTr("Rate limited"); value: String(DhtController.stats.rateLimited) }
                StatTile { label: qsTr("Active lookups"); value: String(DhtController.stats.activeLookups) }
                StatTile { label: qsTr("Stored peers"); value: qsTr("%1 across %2 infohashes").arg(DhtController.stats.storedPeers).arg(DhtController.stats.storedInfohashes) }
            }
        }
    }
}
