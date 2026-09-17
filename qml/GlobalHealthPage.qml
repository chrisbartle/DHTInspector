pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    readonly property bool running: DhtController.running
    readonly property var crawl: DhtController.crawl
    readonly property var stats: DhtController.networkStats
    readonly property var totals: stats.totals || { heardIps: 0, connectedIps: 0, answeringIps: 0, unroutableIps: 0, multiNodeIps: 0, maxNodesPerIp: 0 }
    readonly property var census: DhtController.census.state !== undefined ? DhtController.census
                                                                            : { state: "idle", totals: [], slices: [] }
    readonly property var rtt: stats.rtt || { samples: 0, bins: [], peakShare: 0, median: -1, p90: -1, p99: -1, mean: -1 }
    readonly property int labelWidth: 150
    property bool byVersion: false

    // Node list cap positions.
    readonly property var capSteps: [100000, 250000, 500000, 1000000, 2000000, 5000000, 10000000, 20000000, 50000000]

    contentWidth: availableWidth
    padding: Theme.spacingLarge
    clip: true

    function count(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0)
    }

    function share(part, whole) {
        return whole > 0 ? (100 * part / whole).toFixed(1) + "%" : "—"
    }

    function bytes(n) {
        if (n < 1024 * 1024)
            return Math.round(n / 1024) + " KiB"
        if (n < 1024 * 1024 * 1024)
            return (n / (1024 * 1024)).toFixed(0) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function duration(seconds) {
        const s = Math.floor(seconds)
        const h = Math.floor(s / 3600)
        const m = Math.floor((s % 3600) / 60)
        if (h > 0)
            return qsTr("%1 h %2 min").arg(h).arg(m)
        if (m > 0)
            return qsTr("%1 min %2 s").arg(m).arg(s % 60)
        return qsTr("%1 s").arg(s)
    }

    function percent(fraction) {
        return (100 * fraction).toFixed(fraction > 0 && fraction < 0.01 ? 2 : 1) + "%"
    }

    function ms(value) {
        return value < 0 ? "—" : count(value) + " ms"
    }

    // Large estimates, rounded for reading: "14.2 million".
    function big(n) {
        if (n >= 1e9)
            return qsTr("%1 billion").arg((n / 1e9).toFixed(2))
        if (n >= 1e6)
            return qsTr("%1 million").arg((n / 1e6).toFixed(n >= 1e7 ? 1 : 2))
        return count(Math.round(n))
    }

    function capIndex(cap) {
        for (let i = 0; i < capSteps.length; ++i) {
            if (capSteps[i] >= cap)
                return i
        }
        return capSteps.length - 1
    }

    function phaseTone(phase) {
        switch (phase) {
        case "discovering":
        case "rechecking":
            return Theme.accent
        case "up to date":
            return Theme.good
        case "waiting":
            return Theme.warn
        default:
            return Theme.textDim
        }
    }

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

    component StatTile: ColumnLayout {
        id: tile

        property string label
        property string value
        property string detail
        property color tone: Theme.text

        Layout.fillWidth: true
        spacing: 2

        Label {
            text: tile.label
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
        }
        Label {
            text: tile.value
            color: tile.tone
            font.pixelSize: Theme.fontSizeNormal
            font.family: Theme.monoFamily
        }
        Label {
            visible: tile.detail !== ""
            text: tile.detail
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }
    }

    // A labelled share bar: name, bar, count, percentage.
    component ShareRow: RowLayout {
        id: shareRow

        property string label
        property int count
        property real share
        property color tone: Theme.accent

        Layout.fillWidth: true
        spacing: Theme.spacing

        Label {
            Layout.preferredWidth: 280
            text: shareRow.label
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 8
            radius: 4
            color: Theme.surfaceAlt

            Rectangle {
                width: parent.width * Math.min(1, Math.max(0, shareRow.share))
                height: parent.height
                radius: 4
                color: shareRow.tone
            }
        }
        Label {
            Layout.preferredWidth: 90
            horizontalAlignment: Text.AlignRight
            text: page.count(shareRow.count)
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.monoFamily
        }
        Label {
            Layout.preferredWidth: 60
            horizontalAlignment: Text.AlignRight
            text: page.percent(shareRow.share)
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.monoFamily
        }
    }

    // Shows the controller's real state; a flip is a request, so a refused
    // one snaps back. Reacts to checkedChanged so assistive tools work too.
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

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingLarge

        // --- the scan ----------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Monitoring")
            subtitle: qsTr("Scans the whole network continuously, as fast as the per-node limit and the send limit allow. Switching it off pauses the scan and keeps what it found; stopping the engine discards everything.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Monitoring") }

                ControlledSwitch {
                    Accessible.name: qsTr("Monitoring")
                    enabled: page.running
                    value: DhtController.monitoring
                    onRequested: on => DhtController.monitoring = on
                }

                Badge {
                    visible: page.running
                    text: page.crawl.phase
                    tone: page.phaseTone(page.crawl.phase)
                }

                Hint {
                    text: {
                        if (!page.running)
                            return qsTr("Start the engine on the Setup tab first.")
                        switch (page.crawl.phase) {
                        case "waiting":
                            return qsTr("Nothing to start from yet: bootstrap or add a node on the Setup tab.")
                        case "discovering":
                            return qsTr("Asking nodes it has not heard from yet; %1 queued, %2 awaiting an answer.")
                                   .arg(page.count(page.crawl.waiting)).arg(page.count(page.crawl.notAsked))
                        case "rechecking":
                            return qsTr("Every node it knows has been asked; going round again, longest-unchecked first.")
                        case "up to date":
                            return qsTr("Every node it knows was checked recently; the next round starts as they fall due.")
                        default:
                            return page.crawl.known > 0 ? qsTr("Paused. What was found is kept.")
                                                        : qsTr("Off.")
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Pace") }

                Label {
                    text: qsTr("%1 queries/s, %2 replies/s")
                          .arg(page.count(page.crawl.queriesPerSecond))
                          .arg(page.count(page.crawl.answersPerSecond))
                    color: page.crawl.phase === "off" ? Theme.textDim : Theme.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                }

                Hint {
                    text: DhtController.sendLimit > 0
                          ? qsTr("Send limit %1 (Setup tab). No node is sent more than two queries a second.").arg(Theme.formatRate(DhtController.sendLimit))
                          : qsTr("Send limit off (Setup tab), so the pace is set by how fast this machine keeps up. No node is sent more than two queries a second.")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Node list cap") }

                ThemedSlider {
                    id: capSlider

                    // Request-and-restore, like the switches.
                    readonly property int wanted: page.capIndex(DhtController.catalogCap)

                    Layout.preferredWidth: 220
                    Accessible.name: qsTr("Node list cap")
                    from: 0
                    to: page.capSteps.length - 1
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: wanted
                    onValueChanged: {
                        const index = Math.round(value)
                        if (index !== wanted) {
                            DhtController.catalogCap = page.capSteps[index]
                            value = Qt.binding(() => capSlider.wanted)
                        }
                    }
                }

                Label {
                    Layout.preferredWidth: 110
                    text: qsTr("%1 nodes").arg(page.count(DhtController.catalogCap))
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                }

                Hint {
                    text: qsTr("Up to about %1 of memory when full; %2 in use now. When full, the longest-silent nodes make way for new ones. Takes effect immediately.")
                          .arg(page.bytes(DhtController.catalogCap * Math.max(page.crawl.bytesPerEntry, 96)))
                          .arg(page.bytes(page.crawl.memoryBytes))
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                columns: page.availableWidth > 900 ? 4 : 2
                columnSpacing: Theme.spacingLarge
                rowSpacing: Theme.spacing

                StatTile { label: qsTr("Queries sent"); value: page.count(page.crawl.queries) }
                StatTile {
                    label: qsTr("Replies")
                    value: page.count(page.crawl.answers + page.crawl.errors)
                    detail: page.crawl.errors > 0 ? qsTr("%1 of them errors").arg(page.count(page.crawl.errors)) : ""
                }
                StatTile { label: qsTr("Timeouts"); value: page.count(page.crawl.timeouts) }
                StatTile {
                    label: qsTr("Not sent")
                    value: page.count(page.crawl.notSent)
                    detail: qsTr("held back by limits or refused by the OS; retried later")
                    tone: page.crawl.notSent > 0 ? Theme.warn : Theme.text
                }
                StatTile { label: qsTr("Awaiting reply"); value: page.count(page.crawl.outstanding) }
                StatTile { label: qsTr("Scanning for"); value: page.duration(page.crawl.monitoredSeconds) }
                StatTile {
                    label: qsTr("Replaced when full")
                    value: page.count(page.crawl.evicted)
                }
                StatTile {
                    label: qsTr("Pace allowance")
                    value: qsTr("%1 per tick").arg(page.count(page.crawl.batch))
                    detail: qsTr("halves when this machine falls behind")
                }
            }
        }

        // --- what it found -----------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Nodes Found")
            subtitle: qsTr("Counted by IP address: node IDs can be changed at will and one address can run many nodes, while addresses change far less often. Coverage is partial by nature: nodes behind NAT often cannot be reached, and read-only nodes are never listed by others.")

            GridLayout {
                Layout.fillWidth: true
                columns: page.availableWidth > 900 ? 5 : 3
                columnSpacing: Theme.spacingLarge
                rowSpacing: Theme.spacing

                StatTile {
                    label: qsTr("Addresses heard about")
                    value: page.count(page.totals.heardIps)
                    detail: qsTr("listed to us by any node")
                }
                StatTile {
                    label: qsTr("Connected")
                    value: page.count(page.totals.connectedIps)
                    detail: qsTr("%1 of those; answered us at least once").arg(page.share(page.totals.connectedIps, page.totals.heardIps))
                    tone: Theme.good
                }
                StatTile {
                    label: qsTr("Answering now")
                    value: page.count(page.totals.answeringIps)
                    detail: qsTr("answered their latest query")
                    tone: Theme.good
                }
                StatTile {
                    label: qsTr("Running several nodes")
                    value: page.count(page.totals.multiNodeIps)
                    detail: page.totals.maxNodesPerIp > 1 ? qsTr("up to %1 on one address").arg(page.count(page.totals.maxNodesPerIp)) : ""
                    tone: page.totals.multiNodeIps > 0 ? Theme.warn : Theme.text
                }
                StatTile {
                    label: qsTr("Unreachable addresses")
                    value: page.count(page.totals.unroutableIps)
                    detail: qsTr("listed, but private or invalid")
                }
            }

            Label {
                Layout.topMargin: Theme.spacing
                text: qsTr("By node (address and port)")
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }

            // Proportions at a glance.
            Rectangle {
                id: bar

                readonly property var parts: [
                    { value: page.crawl.responsive, color: Theme.good },
                    { value: page.crawl.gone, color: Theme.warn },
                    { value: page.crawl.silent, color: Theme.bad },
                    { value: page.crawl.notAsked, color: Theme.accent },
                    { value: page.crawl.unroutable, color: Theme.textFaint }
                ]

                Layout.fillWidth: true
                implicitHeight: 10
                radius: 5
                color: Theme.surfaceAlt
                clip: true
                visible: page.crawl.known > 0

                Row {
                    anchors.fill: parent

                    Repeater {
                        model: bar.parts

                        delegate: Rectangle {
                            required property var modelData
                            width: page.crawl.known > 0 ? bar.width * modelData.value / page.crawl.known : 0
                            height: bar.height
                            color: modelData.color
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: page.availableWidth > 900 ? 6 : 3
                columnSpacing: Theme.spacingLarge
                rowSpacing: Theme.spacing

                StatTile { label: qsTr("Nodes known"); value: page.count(page.crawl.known) }
                StatTile {
                    label: qsTr("Responsive")
                    value: page.count(page.crawl.responsive)
                    detail: page.share(page.crawl.responsive, page.crawl.known)
                    tone: Theme.good
                }
                StatTile {
                    label: qsTr("Gone")
                    value: page.count(page.crawl.gone)
                    detail: qsTr("%1, stopped answering").arg(page.share(page.crawl.gone, page.crawl.known))
                    tone: Theme.warn
                }
                StatTile {
                    label: qsTr("Silent")
                    value: page.count(page.crawl.silent)
                    detail: qsTr("%1, never answered").arg(page.share(page.crawl.silent, page.crawl.known))
                    tone: Theme.bad
                }
                StatTile {
                    label: qsTr("Awaiting an answer")
                    value: page.count(page.crawl.notAsked)
                    detail: qsTr("%1, new or on a second try").arg(page.share(page.crawl.notAsked, page.crawl.known))
                    tone: Theme.accent
                }
                StatTile {
                    label: qsTr("Unreachable")
                    value: page.count(page.crawl.unroutable)
                    detail: qsTr("%1, listed but never asked").arg(page.share(page.crawl.unroutable, page.crawl.known))
                }
            }
        }

        // --- network size ------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Network Size")
            subtitle: qsTr("How many IP addresses take part: those heard about, and those that actually answered.")

            Label {
                text: qsTr("Precise count")
                color: Theme.text
                font.pixelSize: Theme.fontSizeNormal
                font.weight: Font.DemiBold
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                ThemedButton {
                    visible: page.census.state !== "running"
                    text: page.census.totals && page.census.totals.length > 0 ? qsTr("Count again") : qsTr("Count precisely")
                    primary: true
                    enabled: page.running
                    onClicked: DhtController.startCensus()
                }
                ThemedButton {
                    visible: page.census.state === "running"
                    text: qsTr("Cancel")
                    onClicked: DhtController.cancelCensus()
                }

                Badge {
                    visible: page.census.state === "running" || page.census.state === "cancelled"
                    text: page.census.state === "running" ? qsTr("counting") : qsTr("cancelled")
                    tone: page.census.state === "running" ? Theme.accent : Theme.warn
                }

                Hint {
                    text: {
                        const c = page.census
                        if (!page.running)
                            return qsTr("Start the engine on the Setup tab first.")
                        if (c.state === "running") {
                            return qsTr("Slice %1 of %2 (%3, 1/%4 of the ID space): round %5, %6 nodes found, %7 answered. %8 queries, %9 so far.")
                                   .arg(c.slicesDone + 1).arg(c.slicesTotal).arg(c.family)
                                   .arg(page.count(Math.pow(2, c.bits))).arg(c.round)
                                   .arg(page.count(c.nodesFound)).arg(page.count(c.nodesAnswered))
                                   .arg(page.count(c.queries)).arg(page.duration(c.seconds))
                        }
                        return qsTr("Takes random slices of the ID space, a few thousand nodes each, and keeps asking the nodes inside until no new ones turn up, so nearly every reachable node there is found. The addresses heard about and those that answered are scaled up to the whole network, with addresses running several nodes scaled less. Takes a few minutes, with or without Monitoring; the quick estimate below, if it has run, sets the slice width.")
                    }
                }
            }

            Repeater {
                model: page.census.totals || []

                delegate: ColumnLayout {
                    id: total

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingSmall
                    spacing: 4

                    RowLayout {
                        spacing: Theme.spacingLarge

                        Badge { text: total.modelData.family; tone: Theme.textDim }

                        StatTile {
                            Layout.fillWidth: false
                            label: qsTr("Addresses heard about")
                            value: qsTr("about %1").arg(page.big(total.modelData.heard))
                            detail: qsTr("95%: %1 to %2").arg(page.big(total.modelData.heardLow)).arg(page.big(total.modelData.heardHigh))
                        }
                        StatTile {
                            Layout.fillWidth: false
                            label: qsTr("Addresses connected")
                            value: qsTr("about %1").arg(page.big(total.modelData.connected))
                            detail: qsTr("95%: %1 to %2").arg(page.big(total.modelData.connectedLow)).arg(page.big(total.modelData.connectedHigh))
                            tone: Theme.good
                        }
                    }

                    Hint {
                        text: qsTr("From %n slice(s), each 1/%1 of the ID space.", "", total.modelData.slices)
                              .arg(page.count(Math.pow(2, total.modelData.bits)))
                              + (page.census.state === "cancelled" ? " " + qsTr("Cancelled before all slices were counted.") : "")
                              + (page.census.state !== "running" ? " " + qsTr("Took %1.").arg(page.duration(page.census.seconds)) : "")
                    }
                }
            }

            // Each slice, for anyone checking the arithmetic.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: (page.census.slices || []).length > 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    Repeater {
                        model: [qsTr("Slice"), qsTr("Heard"), qsTr("Answered"), qsTr("Scaled: heard"), qsTr("Scaled: answered"), qsTr("Rounds"), qsTr("Queries"), qsTr("Time")]
                        delegate: Label {
                            required property string modelData
                            Layout.preferredWidth: 110
                            text: modelData
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }

                Repeater {
                    model: page.census.slices || []

                    delegate: RowLayout {
                        id: sliceRow

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        Repeater {
                            model: [
                                qsTr("%1 #%2").arg(sliceRow.modelData.family).arg(sliceRow.index + 1),
                                qsTr("%1 addr.").arg(page.count(sliceRow.modelData.ipsHeard)),
                                qsTr("%1 addr.").arg(page.count(sliceRow.modelData.ipsAnswered)),
                                page.big(sliceRow.modelData.heardEstimate),
                                page.big(sliceRow.modelData.connectedEstimate),
                                String(sliceRow.modelData.rounds),
                                page.count(sliceRow.modelData.queries),
                                page.duration(sliceRow.modelData.seconds)
                            ]
                            delegate: Label {
                                required property string modelData
                                Layout.preferredWidth: 110
                                text: modelData
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeSmall
                                font.family: Theme.monoFamily
                            }
                        }
                    }
                }
            }

            Label {
                Layout.topMargin: Theme.spacing
                text: qsTr("Quick estimate")
                color: Theme.text
                font.pixelSize: Theme.fontSizeNormal
                font.weight: Font.DemiBold
            }

            Repeater {
                model: DhtController.sizeEstimates

                delegate: ColumnLayout {
                    id: estimate

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 2

                    RowLayout {
                        spacing: Theme.spacing

                        Badge { text: estimate.modelData.family; tone: Theme.textDim }

                        Label {
                            text: estimate.modelData.samples > 0
                                  ? qsTr("roughly %1 answering addresses").arg(page.big(estimate.modelData.median))
                                  : (DhtController.monitoring ? qsTr("waiting for the first lookups")
                                                              : qsTr("switch Monitoring on for a quick estimate"))
                            color: estimate.modelData.samples > 0 ? Theme.text : Theme.textFaint
                            font.pixelSize: Theme.fontSizeNormal
                        }
                    }

                    Hint {
                        visible: estimate.modelData.samples > 0
                        text: qsTr("From %n random lookup(s), about %1 nodes, at %2 nodes per address seen so far (95%: %3 to %4 addresses). This is a rough figure: it rests on assumptions about how node IDs are spread and how complete lookups are, and on the live network it has come out well above the precise count. When the two disagree, go by the precise count. The scan has reached %5 of them (%6).", "", estimate.modelData.samples)
                              .arg(page.big(estimate.modelData.nodes))
                              .arg(estimate.modelData.nodesPerIp.toFixed(2))
                              .arg(page.big(estimate.modelData.low)).arg(page.big(estimate.modelData.high))
                              .arg(page.count(estimate.modelData.answeringIps))
                              .arg(page.percent(estimate.modelData.coverage))
                    }
                }
            }

            EmptyState {
                visible: DhtController.sizeEstimates.length === 0
                message: qsTr("Start the engine to estimate")
            }
        }

        // --- statistics ----------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Label {
                text: qsTr("Statistics")
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
            }

            Repeater {
                model: DhtController.ipv6Enabled
                       ? [{ key: "all", label: qsTr("Both") }, { key: "ipv4", label: qsTr("IPv4") }, { key: "ipv6", label: qsTr("IPv6") }]
                       : []

                delegate: ThemedButton {
                    required property var modelData
                    text: modelData.label
                    primary: DhtController.statsFamily === modelData.key
                    Accessible.name: qsTr("Show statistics for %1").arg(modelData.label)
                    onClicked: DhtController.statsFamily = modelData.key
                }
            }

            Hint {
                text: !page.stats.available
                      ? qsTr("Appear once the scan has run. Everything here is as seen from this machine, over the addresses that answered their latest query.")
                      : qsTr("Over %1 answering addresses, as seen from this machine; an address running several nodes counts once, split between them. Recomputed every few seconds; the last pass took %2 ms.")
                        .arg(page.count(page.stats.answeringIps)).arg(page.stats.computeMs)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: page.availableWidth > 900 ? 2 : 1
            columnSpacing: Theme.spacingLarge
            rowSpacing: Theme.spacingLarge

            // Round trips --------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Round Trip")
                subtitle: qsTr("How long answering addresses took to answer their latest query. Distance from this machine dominates.")

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: Theme.spacingLarge
                    visible: page.rtt.samples > 0

                    StatTile { label: qsTr("Median"); value: page.ms(page.rtt.median) }
                    StatTile { label: qsTr("90th percentile"); value: page.ms(page.rtt.p90) }
                    StatTile { label: qsTr("99th percentile"); value: page.ms(page.rtt.p99) }
                    StatTile { label: qsTr("Mean"); value: page.ms(Math.round(page.rtt.mean)) }
                }

                // Histogram: 50 ms bins to a second, then coarser.
                Item {
                    id: histogram

                    readonly property var bins: page.rtt.bins
                    readonly property real peak: page.rtt.peakShare

                    Layout.fillWidth: true
                    implicitHeight: 110
                    visible: page.rtt.samples > 0

                    Row {
                        id: bars
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 90
                        spacing: 2

                        Repeater {
                            model: histogram.bins

                            delegate: Rectangle {
                                id: barItem

                                required property var modelData

                                width: (bars.width - bars.spacing * (histogram.bins.length - 1)) / Math.max(1, histogram.bins.length)
                                height: bars.height
                                color: "transparent"

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: histogram.peak > 0 ? Math.max(barItem.modelData.count > 0 ? 1 : 0, parent.height * barItem.modelData.share / histogram.peak) : 0
                                    radius: 1
                                    color: barItem.modelData.from >= 1000 ? Theme.warn : Theme.accent
                                }

                                HoverHandler { id: barHover }
                                ToolTip.visible: barHover.hovered
                                ToolTip.text: qsTr("%1: %2 addresses (%3)")
                                              .arg(barItem.modelData.to > 3000 ? qsTr("%1 ms and slower").arg(barItem.modelData.from)
                                                                                : qsTr("%1–%2 ms").arg(barItem.modelData.from).arg(barItem.modelData.to))
                                              .arg(page.count(barItem.modelData.count))
                                              .arg(page.percent(barItem.modelData.share))
                            }
                        }
                    }

                    Repeater {
                        model: [{ at: 0, label: "0" }, { at: 5, label: "250 ms" }, { at: 10, label: "500 ms" },
                                { at: 15, label: "750 ms" }, { at: 20, label: "1 s" }, { at: 22, label: "2 s+" }]

                        delegate: Label {
                            required property var modelData
                            x: Math.min(histogram.width - width,
                                        modelData.at * (bars.width + bars.spacing) / Math.max(1, histogram.bins.length))
                            anchors.bottom: parent.bottom
                            text: modelData.label
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSizeSmall - 1
                        }
                    }
                }

                EmptyState {
                    visible: page.rtt.samples === 0
                    message: qsTr("No round trips measured yet")
                }
            }

            // Clients ----------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.columnSpan: parent.columns
                title: qsTr("Clients")
                subtitle: qsTr("Implementation and version mix, read from each node's 'v' field, per answering address. Self-reported: a node can claim to be anything.")

                RowLayout {
                    spacing: Theme.spacingSmall

                    ThemedButton {
                        text: qsTr("By client")
                        primary: !page.byVersion
                        onClicked: page.byVersion = false
                    }
                    ThemedButton {
                        text: qsTr("By version")
                        primary: page.byVersion
                        onClicked: page.byVersion = true
                    }

                    Hint {
                        Layout.leftMargin: Theme.spacing
                        visible: page.stats.available === true
                        text: qsTr("%1 distinct clients, %2 distinct versions. %3 of addresses send no version at all.")
                              .arg(page.count(page.stats.distinctClients || 0))
                              .arg(page.count(page.stats.distinctVersions || 0))
                              .arg(page.percent(page.stats.noVersionShare || 0))
                    }
                }

                Repeater {
                    model: page.stats.available ? (page.byVersion ? page.stats.versions : page.stats.clients) : []

                    delegate: ShareRow {
                        required property var modelData
                        label: modelData.version !== "" ? modelData.name + "  " + modelData.version : modelData.name
                        count: modelData.count
                        share: modelData.share
                        tone: modelData.kind === "known" ? Theme.accent
                              : modelData.kind === "absent" || modelData.kind === "other" ? Theme.textDim
                              : Theme.warn
                    }
                }

                EmptyState {
                    visible: !page.stats.available || page.stats.answeringIps === 0
                    message: qsTr("No answering addresses yet")
                }
            }

            // BEP 42 -------------------------------------------------------------
            Panel {
                id: bep42Panel

                readonly property var counts: page.stats.bep42 || { compliant: 0, noncompliant: 0, exempt: 0, unknown: 0 }
                readonly property int total: counts.compliant + counts.noncompliant + counts.exempt + counts.unknown

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("BEP 42")
                subtitle: qsTr("Whether answering nodes' IDs match their IP address, which makes flooding the network with fake nodes harder. Many clients predate BEP 42, so non-compliance alone is not suspicious.")

                ShareRow {
                    label: qsTr("Compliant")
                    count: bep42Panel.counts.compliant
                    share: bep42Panel.total > 0 ? bep42Panel.counts.compliant / bep42Panel.total : 0
                    tone: Theme.good
                }
                ShareRow {
                    label: qsTr("Not compliant")
                    count: bep42Panel.counts.noncompliant
                    share: bep42Panel.total > 0 ? bep42Panel.counts.noncompliant / bep42Panel.total : 0
                    tone: Theme.bad
                }
                ShareRow {
                    label: qsTr("Exempt (local address)")
                    count: bep42Panel.counts.exempt
                    share: bep42Panel.total > 0 ? bep42Panel.counts.exempt / bep42Panel.total : 0
                    tone: Theme.textDim
                }
                ShareRow {
                    label: qsTr("Unknown (no node ID)")
                    count: bep42Panel.counts.unknown
                    share: bep42Panel.total > 0 ? bep42Panel.counts.unknown / bep42Panel.total : 0
                    tone: Theme.textFaint
                }
            }

            // Ports ------------------------------------------------------------
            Panel {
                id: portsPanel

                readonly property var ports: page.stats.ports || { distinct: 0, defaultCount: 0, defaultShare: 0, top: [] }

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Ports")
                subtitle: qsTr("Which UDP ports answering addresses use. Most clients pick a random port; 6881 is the traditional default.")

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingLarge

                    StatTile {
                        label: qsTr("On 6881")
                        value: page.count(portsPanel.ports.defaultCount)
                        detail: page.percent(portsPanel.ports.defaultShare)
                    }
                    StatTile { label: qsTr("Distinct ports"); value: page.count(portsPanel.ports.distinct) }
                }

                Repeater {
                    model: portsPanel.ports.top

                    delegate: ShareRow {
                        required property var modelData
                        label: String(modelData.port)
                        count: modelData.count
                        share: modelData.share
                        tone: Theme.accent
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: qsTr("Optional Features")
                subtitle: qsTr("Support for BEP 32 (IPv6 node lists), BEP 44 (stored items) and BEP 51 (infohash sampling).")
                EmptyState { message: qsTr("Not measured yet") }
            }
        }

        NodeListPanel {
            Layout.fillWidth: true
            active: page.visible
        }
    }
}
