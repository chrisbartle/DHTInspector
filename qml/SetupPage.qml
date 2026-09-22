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

    function count(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0)
    }

    // Contact limit positions, in new endpoints a second; one past the end
    // means unlimited. Multiplied by the window a router is assumed to hold
    // an entry for, these run from a few hundred entries to far more than a
    // home router keeps.
    readonly property var contactSteps: [5, 10, 25, 50, 100, 200, 400, 800, 1600]

    function contactIndex(perSecond) {
        if (perSecond <= 0)
            return contactSteps.length
        for (let i = 0; i < contactSteps.length; ++i) {
            if (contactSteps[i] >= perSecond)
                return i
        }
        return contactSteps.length - 1
    }

    function contactAt(index) {
        return index >= contactSteps.length ? 0 : contactSteps[index]
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
        property string nodeId
        property bool familyEnabled: true

        signal nodeIdEdited(string value)
        signal randomizeRequested()

        readonly property bool live: page.running && block.familyEnabled && block.status.bound
        readonly property string idError: block.familyEnabled ? DhtController.validateNodeId(block.nodeId) : ""

        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Label {
                Layout.fillWidth: true
                text: block.family
                color: Theme.text
                font.pixelSize: Theme.fontSizeNormal
                font.weight: Font.DemiBold
            }

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

            ThemedTextField {
                Layout.fillWidth: true
                Accessible.name: qsTr("%1 node ID").arg(block.family)
                text: block.nodeId
                enabled: block.familyEnabled
                readOnly: page.running
                selectByMouse: true
                invalid: block.idError !== ""
                // textChanged rather than textEdited: accessibility tools set the
                // text without emitting textEdited. Updates coming from the
                // controller match nodeId and are not echoed back.
                onTextChanged: if (text !== block.nodeId) block.nodeIdEdited(text)
            }

            CopyButton {
                value: block.nodeId
                what: qsTr("%1 node ID").arg(block.family)
            }

            ThemedButton {
                Accessible.name: qsTr("Randomize %1 node ID").arg(block.family)
                text: qsTr("Randomize")
                enabled: !page.running && block.familyEnabled
                onClicked: block.randomizeRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: page.labelWidth + Theme.spacing
            visible: block.idError !== ""
            text: block.idError.charAt(0).toUpperCase() + block.idError.slice(1)
            color: Theme.bad
            font.pixelSize: Theme.fontSizeSmall
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            FieldLabel { text: qsTr("External IP") }

            Label {
                id: externalIp

                readonly property bool known: block.live && block.status.externalAddress !== ""

                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                font.pixelSize: Theme.fontSizeSmall
                font.family: known ? Theme.monoFamily : Qt.application.font.family
                color: known ? Theme.text : block.live || page.running ? Theme.textDim : Theme.textFaint
                wrapMode: Text.WordWrap
                text: {
                    if (!block.familyEnabled)
                        return qsTr("%1 is off").arg(block.family)
                    if (!page.running)
                        return qsTr("Derived once the engine is running")
                    if (!block.status.bound)
                        return block.status.error !== "" ? block.status.error : qsTr("Not running")
                    if (!known)
                        return qsTr("Not derived yet: waiting for other nodes to agree")
                    return block.status.externalAddress
                }
            }

            CopyButton {
                visible: externalIp.known
                value: block.status.externalAddress
                what: qsTr("external IP")
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            FieldLabel { text: qsTr("BEP 42 compliance") }

            Badge {
                readonly property string compliance: block.live ? block.status.bep42 : "unknown"

                text: Theme.bep42Text(compliance)
                // With BEP 42 off, non-compliance is expected rather than a fault.
                tone: !DhtController.bep42Enabled && compliance === "noncompliant" ? Theme.textDim
                                                                                    : Theme.bep42Color(compliance)
            }

            Item { Layout.fillWidth: true }
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
            subtitle: qsTr("Stopping the engine discards everything it holds: routing tables, stored peers and tokens. The node IDs under Node Identity are kept.")

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
                    Accessible.name: qsTr("IPv6")
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

                FieldLabel { text: qsTr("BEP 42") }

                ControlledSwitch {
                    Accessible.name: qsTr("BEP 42")
                    enabled: !page.running
                    value: DhtController.bep42Enabled
                    onRequested: on => DhtController.bep42Enabled = on
                }

                Hint {
                    text: qsTr("Derives this node's ID from its external IP address once other nodes agree on it, which makes it hard to flood the DHT with fake nodes. Replies also tell each node the address we see it from. When off, each node uses exactly the ID set under Node Identity and sends no address.")
                          + (page.running ? " " + qsTr("Stop the engine to change.") : "")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Read-only") }

                ControlledSwitch {
                    Accessible.name: qsTr("Read-only")
                    value: DhtController.readOnlyMode
                    onRequested: on => DhtController.readOnlyMode = on
                }

                Badge {
                    visible: page.running && DhtController.readOnlyMode
                    text: qsTr("answering nothing")
                    tone: Theme.warn
                }

                Hint {
                    text: {
                        if (!DhtController.readOnlyMode)
                            return qsTr("Marks every query we send with the BEP 43 \"ro\" flag, so well-behaved nodes keep us out of their routing tables. We then answer no incoming queries and store nothing for anyone; our own searches still work. Can be changed while running.")
                        const dropped = DhtController.stats.readOnlyDropped
                        if (page.running && dropped > 0)
                            return qsTr("Queries we send carry \"ro\" and we answer none. %n received so far ignored.", "", dropped)
                        return qsTr("Queries we send carry \"ro\" and we answer none. Nothing new can be stored here while this is on.")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Contact limit") }

                ThemedSlider {
                    id: contactLimitSlider

                    // Always shows the controller's value. A move is sent as
                    // a request and the binding restored, the same pattern as
                    // the switches, so assistive tools that set the value
                    // directly work too.
                    readonly property int wanted: page.contactIndex(DhtController.contactLimit)

                    Layout.preferredWidth: 220
                    Accessible.name: qsTr("Contact limit")
                    from: 0
                    to: page.contactSteps.length
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    live: true
                    value: wanted
                    onValueChanged: {
                        const index = Math.round(value)
                        if (index !== wanted) {
                            DhtController.contactLimit = page.contactAt(index)
                            value = Qt.binding(() => contactLimitSlider.wanted)
                        }
                    }
                }

                Label {
                    Layout.preferredWidth: 90
                    text: DhtController.contactLimit > 0 ? qsTr("%1/s").arg(DhtController.contactLimit)
                                                         : qsTr("Unlimited")
                    color: DhtController.contactLimit > 0 ? Theme.text : Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                }

                Hint {
                    text: {
                        let t = qsTr("How many endpoints a second this node may contact afresh, both address families together. A router keeps one entry per endpoint it sees us talk to, for a minute or two, and those tables hold only a few thousand. Queries to an endpoint contacted within the last %1 minutes are free, and replies are never held back, so incoming queries are always answered; our own queries wait their turn instead. Takes effect immediately. Separately, no single host is ever sent more than two queries a second.")
                                .arg(Math.round(DhtController.contactWindowSeconds / 60))
                        if (DhtController.contactLimit > 0) {
                            t += " " + qsTr("At %1 a second a router would hold at most about %2 entries, fewer if it forgets them sooner.")
                                       .arg(DhtController.contactLimit)
                                       .arg(page.count(DhtController.contactLimit * DhtController.contactWindowSeconds))
                        }
                        if (page.running) {
                            t += " " + qsTr("Contacting %1 a second now, with about %2 entries held.")
                                       .arg(Number(DhtController.stats.newContactsPerSecond).toFixed(1))
                                       .arg(page.count(DhtController.stats.trackedContacts))
                        }
                        return t
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Port forwarding") }

                ControlledSwitch {
                    Accessible.name: qsTr("Port forwarding")
                    value: DhtController.portForwarding
                    onRequested: on => DhtController.portForwarding = on
                }

                Badge {
                    visible: page.running && DhtController.portForwarding
                    text: DhtController.portMapping.state
                    tone: page.mappingTone(DhtController.portMapping.state)
                }

                Hint {
                    Layout.maximumWidth: implicitWidth
                    color: DhtController.portMapping.state === "failed" ? Theme.bad : Theme.textFaint
                    text: {
                        if (!DhtController.portForwarding)
                            return qsTr("Asks the gateway to forward the port (PCP, then NAT-PMP, then UPnP)")
                        if (!page.running)
                            return qsTr("Mapping starts with the engine")
                        const m = DhtController.portMapping
                        if (m.state === "mapped" && m.externalAddress !== "")
                            return qsTr("%1:%2 via %3").arg(m.externalAddress).arg(m.externalPort).arg(m.protocol)
                        return m.message
                    }
                }

                CopyButton {
                    readonly property var m: DhtController.portMapping
                    visible: DhtController.portForwarding && page.running
                             && m.state === "mapped" && m.externalAddress !== ""
                    value: visible ? m.externalAddress + ":" + m.externalPort : ""
                    what: qsTr("forwarded address")
                }

                Item { Layout.fillWidth: true }
            }
        }

        // --- Identity --------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Node Identity")
            subtitle: DhtController.bep42Enabled
                      ? qsTr("Each address family runs its own node with its own ID, editable while the engine is stopped. With BEP 42 on, once enough nodes agree on our external IP, a non-compliant ID is replaced by a compliant one and the field shows the new value.")
                      : qsTr("Each address family runs its own node with its own ID, editable while the engine is stopped. BEP 42 is off, so the engine uses exactly the ID given.")

            IdentityBlock {
                family: qsTr("IPv4")
                status: DhtController.ipv4
                nodeId: DhtController.nodeIdV4
                onNodeIdEdited: value => DhtController.nodeIdV4 = value
                onRandomizeRequested: DhtController.randomizeNodeId(false)
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
                nodeId: DhtController.nodeIdV6
                onNodeIdEdited: value => DhtController.nodeIdV6 = value
                onRandomizeRequested: DhtController.randomizeNodeId(true)
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

                Repeater {
                    model: DhtController.bootstrapRouters()

                    delegate: AddressLink {
                        required property string modelData
                        address: modelData
                    }
                }

                Item { Layout.fillWidth: true }
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
                      ? qsTr("%1 entries: routing table nodes plus bootstrap and injected endpoints not in the table. Click an address to probe that node.").arg(DhtController.nodes.count)
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

                            AddressLink {
                                Layout.fillWidth: page.columns[0].width === 0
                                Layout.preferredWidth: page.columns[0].width
                                address: row.address
                                elide: Text.ElideMiddle
                            }
                            // Shows the short ID, copies the whole one.
                            CopyableText {
                                Layout.preferredWidth: page.columns[1].width
                                text: row.nodeIdShort
                                value: row.nodeId
                                color: Theme.textDim
                                what: qsTr("node ID")
                            }
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
            subtitle: qsTr("Totals since the engine started, both address families combined. So that no node is overwhelmed, queries to any one IP address are held to two a second; extra ones wait their turn, and are refused only if more than 64 are already waiting.")

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
                StatTile { label: qsTr("Dropped (read-only)"); value: String(DhtController.stats.readOnlyDropped) }
                StatTile {
                    label: qsTr("New contacts / tracked")
                    value: qsTr("%1 / %2").arg(page.count(DhtController.stats.newContacts))
                                          .arg(page.count(DhtController.stats.trackedContacts))
                }
                StatTile {
                    label: qsTr("Rate out / in")
                    value: Theme.formatRate(DhtController.stats.bytesOutPerSecond) + " / " + Theme.formatRate(DhtController.stats.bytesInPerSecond)
                }
                StatTile {
                    label: qsTr("Held back / refused")
                    value: qsTr("%1 / %2").arg(DhtController.stats.queriesDelayed).arg(DhtController.stats.queriesRefused)
                           + (DhtController.stats.queriesWaiting > 0 ? qsTr(" (%1 waiting)").arg(DhtController.stats.queriesWaiting) : "")
                }
                StatTile { label: qsTr("Active lookups"); value: String(DhtController.stats.activeLookups) }
                StatTile { label: qsTr("Stored peers"); value: qsTr("%1 across %2 infohashes").arg(DhtController.stats.storedPeers).arg(DhtController.stats.storedInfohashes) }
            }
        }
    }
}
