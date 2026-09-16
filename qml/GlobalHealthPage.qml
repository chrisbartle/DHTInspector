pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    readonly property bool running: DhtController.running
    readonly property var crawl: DhtController.crawl
    readonly property int labelWidth: 150

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
            subtitle: qsTr("Every node the scan has heard of. Coverage is partial by nature: nodes behind NAT often cannot be reached, and read-only nodes are never listed by others.")

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

                StatTile { label: qsTr("Known"); value: page.count(page.crawl.known) }
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
                    label: qsTr("Unreachable addresses")
                    value: page.count(page.crawl.unroutable)
                    detail: qsTr("%1, listed but never asked").arg(page.share(page.crawl.unroutable, page.crawl.known))
                }
            }
        }

        // --- still to come -----------------------------------------------------
        GridLayout {
            Layout.fillWidth: true
            columns: page.availableWidth > 900 ? 2 : 1
            columnSpacing: Theme.spacingLarge
            rowSpacing: Theme.spacingLarge

            Panel {
                Layout.fillWidth: true
                title: qsTr("Network Size")
                subtitle: qsTr("Estimated from how densely nodes fill the ID space around random targets.")
                EmptyState { message: qsTr("Not measured yet") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Clients")
                subtitle: qsTr("Implementation and version mix, read from each node's 'v' field.")
                EmptyState { message: qsTr("Not measured yet") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Round Trip")
                subtitle: qsTr("Median and percentiles as seen from here.")
                EmptyState { message: qsTr("Not measured yet") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("BEP 42 and Optional Features")
                subtitle: qsTr("Secure node IDs, and support for BEP 32, 44 and 51.")
                EmptyState { message: qsTr("Not measured yet") }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Nodes")
            subtitle: qsTr("Every node found, filterable, with each address linking to the Probe tab.")
            EmptyState {
                message: qsTr("Not built yet")
                implicitHeight: 140
            }
        }
    }
}
