pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Every node the scan has found, filtered, sorted and paged by the engine.
// An address opens the node on the Probe tab.
Panel {
    id: panel

    // Set by the page: whether the list should keep itself up to date.
    property bool active: false
    readonly property bool running: DhtController.running
    readonly property var info: DhtController.nodeListInfo
    readonly property int pageSize: 100

    // Filters, as the controller takes them.
    property string family: "any"
    property var states: ["responsive", "gone", "silent", "awaiting", "unreachable"]
    property string client: ""
    property string version: ""
    property string bep42: "any"
    property string minRtt: ""
    property string maxRtt: ""
    property string address: ""
    property string port: ""
    property string minNodes: ""
    property string idPrefix: ""
    property string feature: ""
    property string suspicion: ""
    property string sort: "address"
    property bool descending: false

    readonly property string addressError: DhtController.validateAddressFilter(address)
    readonly property string idPrefixError: DhtController.validateIdPrefixFilter(idPrefix)
    readonly property string filterError: addressError !== "" ? addressError : idPrefixError

    title: qsTr("Nodes")
    subtitle: qsTr("Every node the scan has found, one row per address and port. Narrow it down with the filters, then click an address to examine that node on the Probe tab.")

    function count(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0)
    }

    function number(text, fallback) {
        const n = parseInt(text)
        return isNaN(n) ? fallback : n
    }

    function query(offset) {
        return {
            family: family,
            states: states,
            client: client,
            version: version,
            bep42: bep42,
            minRtt: number(minRtt, -1),
            maxRtt: number(maxRtt, -1),
            address: address,
            port: number(port, 0),
            minNodes: number(minNodes, 0),
            idPrefix: idPrefix,
            feature: feature,
            suspicion: suspicion,
            sort: sort,
            descending: descending,
            offset: offset,
            limit: pageSize
        }
    }

    function apply() {
        DhtController.setNodeQuery(query(0))
    }

    // Typing waits for a pause before querying.
    function changed() {
        applyTimer.restart()
    }

    function toggleState(name) {
        const next = states.slice()
        const i = next.indexOf(name)
        if (i >= 0)
            next.splice(i, 1)
        else
            next.push(name)
        states = next
        changed()
    }

    function reset() {
        clearFilters()
        apply()
    }

    function clearFilters() {
        family = "any"
        states = ["responsive", "gone", "silent", "awaiting", "unreachable"]
        client = ""
        version = ""
        bep42 = "any"
        minRtt = ""
        maxRtt = ""
        address = ""
        port = ""
        minNodes = ""
        idPrefix = ""
        feature = ""
        suspicion = ""
        sort = "address"
        descending = false
        syncBoxes()
    }

    // A choice made in a drop-down replaces its binding, so set them.
    function syncBoxes() {
        familyBox.currentIndex = familyBox.indexOfValue(family)
        bep42Box.currentIndex = bep42Box.indexOfValue(bep42)
        featureBox.currentIndex = featureBox.indexOfValue(feature)
        suspicionBox.currentIndex = suspicionBox.indexOfValue(suspicion)
        sortBox.currentIndex = sortBox.indexOfValue(sort)
        clientBox.refresh()
        versionBox.refresh()
    }

    // Shows one group from elsewhere on the page: clears the filters, then
    // applies the ones given (any of the filter properties above).
    function showGroup(filters) {
        clearFilters()
        for (const key in filters)
            panel[key] = filters[key]
        syncBoxes()
        apply()
    }

    Timer {
        id: applyTimer
        interval: 300
        onTriggered: panel.apply()
    }

    // Keep the page current while it is on screen, less often if queries
    // are expensive.
    Timer {
        interval: Math.max(3000, 20 * (panel.info.queryMs || 0))
        repeat: true
        running: panel.active && panel.running
        triggeredOnStart: true
        onTriggered: {
            if (!panel.info.loaded)
                panel.apply()
            else
                DhtController.refreshNodeList()
        }
    }

    component FilterLabel: Label {
        color: Theme.textDim
        font.pixelSize: Theme.fontSizeSmall
    }

    component SmallField: ThemedTextField {
        Layout.preferredWidth: 80
        validator: IntValidator { bottom: 0; top: 65535 }
    }

    // --- filters -----------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacing

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Family") }
            ThemedComboBox {
                id: familyBox
                Accessible.name: qsTr("Family filter")
                implicitWidth: 110
                model: [{ label: qsTr("Any"), value: "any" }, { label: "IPv4", value: "ipv4" }, { label: "IPv6", value: "ipv6" }]
                currentIndex: indexOfValue(panel.family)
                Component.onCompleted: currentIndex = indexOfValue(panel.family)
                onCurrentValueChanged: if (currentValue !== undefined && currentValue !== panel.family) { panel.family = currentValue; panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Status") }
            Row {
                spacing: 4
                Repeater {
                    model: [{ name: "responsive", label: qsTr("Responsive") }, { name: "awaiting", label: qsTr("Awaiting") },
                            { name: "gone", label: qsTr("Gone") }, { name: "silent", label: qsTr("Silent") },
                            { name: "unreachable", label: qsTr("Unreachable") }]
                    delegate: ThemedButton {
                        required property var modelData
                        text: modelData.label
                        primary: panel.states.indexOf(modelData.name) >= 0
                        Accessible.role: Accessible.CheckBox
                        Accessible.checkable: true
                        Accessible.checked: primary
                        Accessible.name: qsTr("Show %1 nodes").arg(modelData.label)
                        onClicked: panel.toggleState(modelData.name)
                    }
                }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Client") }
            ThemedComboBox {
                id: clientBox

                // The list of clients grows while scanning; it is refreshed
                // only when opened, so the choice is never reset under you.
                property bool refreshing: false
                function refresh() {
                    refreshing = true
                    const names = DhtController.clientNames
                    const options = [{ label: qsTr("Any client"), value: "" }]
                    for (const n of names)
                        options.push({ label: n, value: n })
                    if (panel.client !== "" && names.indexOf(panel.client) < 0)
                        options.push({ label: panel.client, value: panel.client })
                    model = options
                    currentIndex = indexOfValue(panel.client)
                    refreshing = false
                }

                Accessible.name: qsTr("Client filter")
                implicitWidth: 230
                Component.onCompleted: refresh()
                Connections {
                    target: clientBox.popup
                    function onAboutToShow() { clientBox.refresh() }
                }
                onCurrentValueChanged: {
                    if (refreshing || currentValue === undefined || currentValue === panel.client)
                        return
                    panel.client = currentValue
                    panel.version = ""
                    versionBox.refresh()
                    panel.changed()
                }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Version") }
            ThemedComboBox {
                id: versionBox

                property bool refreshing: false
                function refresh() {
                    refreshing = true
                    const options = [{ label: qsTr("Any version"), value: "" }]
                    if (panel.client !== "") {
                        for (const v of DhtController.versionsFor(panel.client))
                            options.push({ label: v, value: v })
                    }
                    if (panel.version !== "" && !options.some(o => o.value === panel.version))
                        options.push({ label: panel.version, value: panel.version })
                    model = options
                    currentIndex = indexOfValue(panel.version)
                    refreshing = false
                }

                Accessible.name: qsTr("Version filter")
                implicitWidth: 140
                enabled: panel.client !== ""
                Component.onCompleted: refresh()
                Connections {
                    target: versionBox.popup
                    function onAboutToShow() { versionBox.refresh() }
                }
                onCurrentValueChanged: {
                    if (refreshing || currentValue === undefined || currentValue === panel.version)
                        return
                    panel.version = currentValue
                    panel.changed()
                }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("BEP 42") }
            ThemedComboBox {
                id: bep42Box
                Accessible.name: qsTr("BEP 42 filter")
                implicitWidth: 140
                model: [{ label: qsTr("Any"), value: "any" }, { label: qsTr("Compliant"), value: "compliant" },
                        { label: qsTr("Not compliant"), value: "noncompliant" }, { label: qsTr("Exempt"), value: "exempt" },
                        { label: qsTr("Unknown"), value: "unknown" }]
                currentIndex: indexOfValue(panel.bep42)
                Component.onCompleted: currentIndex = indexOfValue(panel.bep42)
                onCurrentValueChanged: if (currentValue !== undefined && currentValue !== panel.bep42) { panel.bep42 = currentValue; panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Address or subnet") }
            ThemedTextField {
                Accessible.name: qsTr("Address filter")
                implicitWidth: 200
                placeholderText: qsTr("e.g. 203.0.113.0/24")
                text: panel.address
                invalid: panel.addressError !== ""
                onTextChanged: if (text !== panel.address) { panel.address = text; if (panel.addressError === "") panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Node ID prefix") }
            ThemedTextField {
                Accessible.name: qsTr("Node ID prefix filter")
                implicitWidth: 170
                placeholderText: qsTr("hex, e.g. a1b2/13")
                font.family: Theme.monoFamily
                text: panel.idPrefix
                invalid: panel.idPrefixError !== ""
                onTextChanged: if (text !== panel.idPrefix) { panel.idPrefix = text; if (panel.idPrefixError === "") panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Feature") }
            ThemedComboBox {
                id: featureBox
                Accessible.name: qsTr("Feature filter")
                implicitWidth: 230
                model: [{ label: qsTr("Any"), value: "" },
                        { label: qsTr("BEP 51 supported"), value: "bep51-yes" },
                        { label: qsTr("BEP 51 not supported"), value: "bep51-no" },
                        { label: qsTr("BEP 44 supported"), value: "bep44-yes" },
                        { label: qsTr("BEP 44 not supported"), value: "bep44-no" },
                        { label: qsTr("BEP 32 lists both families"), value: "bep32-yes" },
                        { label: qsTr("BEP 32 one family only"), value: "bep32-no" },
                        { label: qsTr("Sends ip field"), value: "ip-yes" },
                        { label: qsTr("No ip field"), value: "ip-no" },
                        { label: qsTr("Unknown query: error 204"), value: "unknown-204" },
                        { label: qsTr("Unknown query: another error"), value: "unknown-error" },
                        { label: qsTr("Unknown query: a normal reply"), value: "unknown-reply" },
                        { label: qsTr("Unknown query: no answer"), value: "unknown-none" },
                        { label: qsTr("Lists unreachable addresses"), value: "bogons" },
                        { label: qsTr("Invents peers"), value: "invents-peers" },
                        { label: qsTr("Checked, invents no peers"), value: "peers-honest" }]
                currentIndex: indexOfValue(panel.feature)
                Component.onCompleted: currentIndex = indexOfValue(panel.feature)
                onCurrentValueChanged: if (currentValue !== undefined && currentValue !== panel.feature) { panel.feature = currentValue; panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Suspicious") }
            ThemedComboBox {
                id: suspicionBox
                Accessible.name: qsTr("Suspicious group filter")
                implicitWidth: 200
                model: [{ label: qsTr("Any node"), value: "" },
                        { label: qsTr("Any signal"), value: "any" },
                        { label: qsTr("Two or more signals"), value: "several" },
                        { label: qsTr("Many nodes on an address"), value: "many" },
                        { label: qsTr("Dense subnet"), value: "subnet" },
                        { label: qsTr("Shared node ID"), value: "sharedId" },
                        { label: qsTr("Dense node IDs"), value: "denseIds" },
                        { label: qsTr("Points to itself"), value: "self" },
                        { label: qsTr("Invents peers"), value: "invents" }]
                currentIndex: indexOfValue(panel.suspicion)
                Component.onCompleted: currentIndex = indexOfValue(panel.suspicion)
                onCurrentValueChanged: if (currentValue !== undefined && currentValue !== panel.suspicion) { panel.suspicion = currentValue; panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Port") }
            SmallField {
                Accessible.name: qsTr("Port filter")
                placeholderText: qsTr("any")
                text: panel.port
                onTextChanged: if (text !== panel.port) { panel.port = text; panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Round trip (ms)") }
            RowLayout {
                spacing: 4
                SmallField {
                    Accessible.name: qsTr("Minimum round trip")
                    placeholderText: qsTr("from")
                    text: panel.minRtt
                    onTextChanged: if (text !== panel.minRtt) { panel.minRtt = text; panel.changed() }
                }
                SmallField {
                    Accessible.name: qsTr("Maximum round trip")
                    placeholderText: qsTr("to")
                    text: panel.maxRtt
                    onTextChanged: if (text !== panel.maxRtt) { panel.maxRtt = text; panel.changed() }
                }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Nodes on the address") }
            SmallField {
                Accessible.name: qsTr("Minimum nodes on the address")
                placeholderText: qsTr("at least")
                text: panel.minNodes
                onTextChanged: if (text !== panel.minNodes) { panel.minNodes = text; panel.changed() }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: qsTr("Sort by") }
            RowLayout {
                spacing: 4
                ThemedComboBox {
                    id: sortBox
                    Accessible.name: qsTr("Sort by")
                    implicitWidth: 150
                    model: [{ label: qsTr("Address"), value: "address" }, { label: qsTr("Round trip"), value: "rtt" },
                            { label: qsTr("Last answer"), value: "lastAnswered" }, { label: qsTr("First seen"), value: "firstSeen" },
                            { label: qsTr("Client"), value: "client" }, { label: qsTr("Nodes on the address"), value: "nodesAtAddress" }]
                    currentIndex: indexOfValue(panel.sort)
                Component.onCompleted: currentIndex = indexOfValue(panel.sort)
                    onCurrentValueChanged: if (currentValue !== undefined && currentValue !== panel.sort) { panel.sort = currentValue; panel.apply() }
                }
                ThemedButton {
                    text: panel.descending ? qsTr("Descending") : qsTr("Ascending")
                    Accessible.name: qsTr("Sort direction: %1").arg(text)
                    onClicked: { panel.descending = !panel.descending; panel.apply() }
                }
            }
        }

        ColumnLayout {
            spacing: 2
            FilterLabel { text: " " }
            ThemedButton {
                text: qsTr("Reset filters")
                onClicked: panel.reset()
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: panel.filterError !== "" || (panel.info.error || "") !== ""
        text: panel.filterError !== "" ? panel.filterError : panel.info.error
        color: Theme.bad
        font.pixelSize: Theme.fontSizeSmall
    }

    // --- summary and paging ------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingSmall
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
            text: {
                const i = panel.info
                if (!panel.running)
                    return qsTr("Start the engine on the Setup tab to see nodes.")
                if (!i.loaded)
                    return qsTr("Loading…")
                if (i.matchedNodes === 0)
                    return DhtController.crawl.known === 0 ? qsTr("No nodes yet. Switch Monitoring on to find some.")
                                                          : qsTr("No nodes match these filters.")
                return qsTr("Showing %1 to %2 of %3 nodes, on %4 addresses. The query took %5 ms.")
                       .arg(panel.count(i.offset + 1))
                       .arg(panel.count(Math.min(i.offset + i.limit, i.matchedNodes)))
                       .arg(panel.count(i.matchedNodes))
                       .arg(panel.count(i.matchedAddresses))
                       .arg(i.queryMs)
            }
        }

        ThemedButton {
            text: qsTr("First")
            enabled: panel.info.loaded && panel.info.offset > 0
            onClicked: DhtController.showNodePage(0)
        }
        ThemedButton {
            text: qsTr("Previous")
            enabled: panel.info.loaded && panel.info.offset > 0
            onClicked: DhtController.showNodePage(Math.max(0, panel.info.offset - panel.info.limit))
        }
        ThemedButton {
            text: qsTr("Next")
            enabled: panel.info.loaded && panel.info.offset + panel.info.limit < panel.info.matchedNodes
            onClicked: DhtController.showNodePage(panel.info.offset + panel.info.limit)
        }
    }

    // --- the table -----------------------------------------------------------
    readonly property var columns: [
        { title: qsTr("Address"), width: 230 },
        { title: qsTr("Status"), width: 90 },
        { title: qsTr("Client"), width: 0 },
        { title: qsTr("Round trip"), width: 80 },
        { title: qsTr("BEP 42"), width: 100 },
        { title: qsTr("On address"), width: 80 },
        { title: qsTr("Last answer"), width: 110 },
        { title: qsTr("First seen"), width: 110 },
        { title: qsTr("Node ID"), width: 130 },
        { title: qsTr("Features"), width: 100 },
        { title: qsTr("Notes"), width: 150 }
    ]

    component Cell: Label {
        property int cellWidth: 0
        Layout.preferredWidth: cellWidth
        Layout.fillWidth: cellWidth === 0
        elide: Text.ElideRight
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.text
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 28
        color: Theme.surfaceAlt
        radius: Theme.radius
        visible: nodeTable.count > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Repeater {
                model: panel.columns
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
        id: nodeTable

        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 26 * 20)
        visible: count > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: DhtController.nodeList
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: row

            required property int index
            required property string address
            required property string state
            required property bool answered
            required property string client
            required property string version
            required property string clientKind
            required property int rtt
            required property string bep42
            required property int nodesAtAddress
            required property string lastAnswered
            required property string firstSeen
            required property string nodeId
            required property string nodeIdShort
            required property string rawVersion
            required property string features
            required property string featureDetail
            required property string suspicion
            required property string problem

            width: ListView.view.width
            height: 26
            color: rowHover.hovered ? Theme.surfaceAlt : row.index % 2 === 0 ? "transparent" : Qt.rgba(1, 1, 1, 0.02)

            HoverHandler { id: rowHover }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                AddressLink {
                    Layout.preferredWidth: panel.columns[0].width
                    address: row.address
                    elide: Text.ElideMiddle
                }
                Cell {
                    cellWidth: panel.columns[1].width
                    text: row.state
                    color: row.state === "responsive" ? Theme.good
                           : row.state === "gone" ? Theme.warn
                           : row.state === "silent" ? Theme.bad
                           : row.state === "awaiting" ? Theme.accent : Theme.textFaint
                }
                Cell {
                    cellWidth: panel.columns[2].width
                    text: !row.answered ? "—" : row.version !== "" ? row.client + "  " + row.version : row.client
                    color: !row.answered ? Theme.textFaint
                           : row.clientKind === "known" ? Theme.text
                           : row.clientKind === "absent" ? Theme.textDim : Theme.warn
                    ToolTip.visible: clientHover.hovered && row.rawVersion !== ""
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("v field: %1").arg(row.rawVersion)
                    HoverHandler { id: clientHover }
                }
                Cell {
                    cellWidth: panel.columns[3].width
                    text: row.rtt < 0 ? "—" : qsTr("%1 ms").arg(row.rtt)
                    color: Theme.textDim
                }
                Cell {
                    cellWidth: panel.columns[4].width
                    text: row.bep42 === "" ? "—" : row.bep42
                    color: row.bep42 === "" ? Theme.textFaint : Theme.bep42Color(row.bep42)
                }
                Cell {
                    cellWidth: panel.columns[5].width
                    text: String(row.nodesAtAddress)
                    color: row.nodesAtAddress > 1 ? Theme.warn : Theme.textDim
                }
                Cell {
                    cellWidth: panel.columns[6].width
                    text: row.lastAnswered
                    color: Theme.textDim
                }
                Cell {
                    cellWidth: panel.columns[7].width
                    text: row.firstSeen
                    color: Theme.textDim
                }
                Cell {
                    cellWidth: panel.columns[8].width
                    text: row.nodeIdShort
                    font.family: Theme.monoFamily
                    color: Theme.textDim
                    ToolTip.visible: idHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: row.nodeId
                    HoverHandler { id: idHover }
                }
                Cell {
                    cellWidth: panel.columns[9].width
                    text: !row.answered ? "—" : row.features === "" ? qsTr("none yet") : row.features
                    color: row.features === "" ? Theme.textFaint : Theme.textDim
                    ToolTip.visible: featureHover.hovered && row.answered
                    ToolTip.delay: 500
                    ToolTip.text: row.featureDetail
                    HoverHandler { id: featureHover }
                }
                Cell {
                    cellWidth: panel.columns[10].width
                    text: row.problem !== "" ? row.problem : row.suspicion !== "" ? row.suspicion : "—"
                    color: row.problem !== "" ? Theme.textDim : row.suspicion !== "" ? Theme.warn : Theme.textFaint
                    ToolTip.visible: notesHover.hovered && text !== "—"
                    ToolTip.delay: 500
                    ToolTip.text: row.problem !== "" ? qsTr("Not contacted: %1").arg(row.problem)
                                                     : qsTr("Suspicious signals for this address: %1").arg(row.suspicion)
                    HoverHandler { id: notesHover }
                }
            }
        }
    }

    EmptyState {
        visible: nodeTable.count === 0
        message: !panel.running ? qsTr("Engine stopped")
                 : !panel.info.loaded ? qsTr("Loading…")
                 : DhtController.crawl.known === 0 ? qsTr("No nodes found yet") : qsTr("No nodes match these filters")
    }

    // --- export ------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing
        spacing: Theme.spacing

        ThemedButton {
            text: qsTr("Export these nodes (CSV)…")
            enabled: panel.running && panel.info.loaded && panel.info.matchedNodes > 0
                     && !DhtController.exportStatus.busy
            onClicked: csvDialog.open()
        }
        ThemedButton {
            text: qsTr("Export summary (JSON)…")
            enabled: panel.running && !DhtController.exportStatus.busy
            onClicked: jsonDialog.open()
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            color: DhtController.exportStatus.error ? Theme.bad : Theme.textDim
            text: DhtController.exportStatus.message !== ""
                  ? DhtController.exportStatus.message
                  : qsTr("Nothing is saved unless you export it. The CSV holds every node matching the filters above, not just this page; the summary holds the statistics, size estimates and precise count.")
        }
    }

    FileDialog {
        id: csvDialog
        title: qsTr("Export nodes")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV files (*.csv)")]
        defaultSuffix: "csv"
        onAccepted: DhtController.exportNodes(selectedFile)
    }

    FileDialog {
        id: jsonDialog
        title: qsTr("Export summary")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON files (*.json)")]
        defaultSuffix: "json"
        onAccepted: DhtController.exportSummary(selectedFile)
    }
}
