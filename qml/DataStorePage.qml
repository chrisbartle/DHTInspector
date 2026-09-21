pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    readonly property bool running: DhtController.running
    readonly property string selected: DhtController.selectedInfohash

    contentWidth: availableWidth
    padding: Theme.spacingLarge
    clip: true

    // Storage can be far larger than the routine engine snapshot, so it is
    // fetched only while this page is on screen.
    Timer {
        interval: 2000
        repeat: true
        running: page.visible && page.running
        triggeredOnStart: true
        onTriggered: DhtController.refreshDataStore()
    }

    component Cell: Label {
        property int cellWidth: 0
        Layout.preferredWidth: cellWidth
        Layout.fillWidth: cellWidth === 0
        elide: Text.ElideRight
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.text
    }

    component TableHeader: Rectangle {
        id: header

        property var titles: []

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
                model: header.titles
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

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingLarge

        // BEP 43 read-only means we answer no queries, so no one can announce
        // a peer or store an item with us. Worth saying plainly here, where an
        // empty or shrinking store would otherwise look like a fault.
        Rectangle {
            Layout.fillWidth: true
            visible: DhtController.readOnlyMode
            implicitHeight: readOnlyNotice.implicitHeight + 2 * Theme.spacing
            radius: Theme.radius
            color: Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.12)
            border.width: 1
            border.color: Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.45)

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacing

                Badge { text: qsTr("read-only"); tone: Theme.warn }

                Label {
                    id: readOnlyNotice

                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSmall
                    text: {
                        const base = qsTr("Read-only mode is on, so this node answers no queries. Nothing new can be announced or stored here, and anything listed below will disappear as it expires. Turn it off on the Setup tab.")
                        const dropped = DhtController.stats.readOnlyDropped
                        return page.running && dropped > 0
                               ? base + " " + qsTr("%n query has been ignored so far.", "", dropped)
                               : base
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Data Store")
            subtitle: qsTr("What this node is holding for other people: peers announced for an infohash, and BEP 44 items stored by key.")

            GridLayout {
                Layout.fillWidth: true
                columns: page.availableWidth > 760 ? 4 : 2
                columnSpacing: Theme.spacingLarge
                rowSpacing: Theme.spacing

                ColumnLayout {
                    spacing: 2
                    Label { text: qsTr("Infohashes"); color: Theme.textDim; font.pixelSize: Theme.fontSizeSmall }
                    Label {
                        text: qsTr("%1 of %2").arg(DhtController.dataStore.infohashCount).arg(DhtController.dataStore.maxInfohashes)
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeNormal
                        font.family: Theme.monoFamily
                    }
                }
                ColumnLayout {
                    spacing: 2
                    Label { text: qsTr("Peers"); color: Theme.textDim; font.pixelSize: Theme.fontSizeSmall }
                    Label {
                        text: String(DhtController.dataStore.peerCount)
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeNormal
                        font.family: Theme.monoFamily
                    }
                }
                ColumnLayout {
                    spacing: 2
                    Label { text: qsTr("Peer lifetime"); color: Theme.textDim; font.pixelSize: Theme.fontSizeSmall }
                    Label {
                        text: qsTr("%1 min").arg(DhtController.dataStore.ttlMinutes)
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeNormal
                        font.family: Theme.monoFamily
                    }
                }
                ColumnLayout {
                    spacing: 2
                    Label { text: qsTr("Peers per infohash"); color: Theme.textDim; font.pixelSize: Theme.fontSizeSmall }
                    Label {
                        text: qsTr("up to %1").arg(DhtController.dataStore.maxPeersPerInfohash)
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeNormal
                        font.family: Theme.monoFamily
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("BEP 44 items: %1 immutable, %2 mutable, of up to %3, each lasting %4 minutes.")
                      .arg(DhtController.dataStore.immutableCount)
                      .arg(DhtController.dataStore.mutableCount)
                      .arg(DhtController.dataStore.maxItems)
                      .arg(DhtController.dataStore.itemTtlMinutes)
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                visible: DhtController.dataStore.truncated
                text: qsTr("Showing the %1 most recently announced infohashes.").arg(DhtController.dataStore.listed)
                color: Theme.warn
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Infohashes")
            subtitle: qsTr("Select one to see the peers stored for it. Entries disappear once their last peer expires.")

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                TableHeader {
                    titles: [
                        { title: qsTr("Infohash"), width: 0 },
                        { title: qsTr("Peers"), width: 70 },
                        { title: qsTr("Last announce"), width: 110 },
                        { title: qsTr("Expires in"), width: 100 }
                    ]
                }

                ListView {
                    id: infohashList

                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: DhtController.storedInfohashes
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: infohashRow

                        required property string infohash
                        required property int peerCount
                        required property string lastAnnounce
                        required property string expiresIn

                        readonly property bool current: infohashRow.infohash === page.selected

                        width: ListView.view.width
                        height: 26
                        color: infohashRow.current ? Qt.rgba(0.29, 0.62, 1.0, 0.18)
                               : hover.hovered ? Theme.surfaceAlt : "transparent"

                        HoverHandler { id: hover }
                        TapHandler {
                            onTapped: (eventPoint) => {
                                const p = copyInfohash.mapFromItem(infohashRow, eventPoint.position)
                                if (!copyInfohash.contains(p))
                                    DhtController.selectInfohash(infohashRow.infohash)
                            }
                        }

                        // Selectable by assistive technology, not just by mouse.
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Infohash %1").arg(infohashRow.infohash)
                        Accessible.selected: infohashRow.current
                        Accessible.onPressAction: DhtController.selectInfohash(infohashRow.infohash)
                        ToolTip.visible: hover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: infohashRow.infohash

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            spacing: Theme.spacingSmall

                            Cell { text: infohashRow.infohash; font.family: Theme.monoFamily }
                            CopyButton {
                                id: copyInfohash
                                value: infohashRow.infohash
                                what: qsTr("infohash")
                            }
                            Cell { cellWidth: 70; text: String(infohashRow.peerCount); color: Theme.textDim }
                            Cell { cellWidth: 110; text: infohashRow.lastAnnounce; color: Theme.textDim }
                            Cell {
                                cellWidth: 100
                                text: infohashRow.expiresIn
                                color: infohashRow.expiresIn === "expired" ? Theme.bad : Theme.textDim
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacingLarge * 2
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: infohashList.count === 0
                        text: page.running
                              ? qsTr("Nothing stored yet. Other nodes announce peers here once this node is among the closest they can find for an infohash.")
                              : qsTr("Engine stopped")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("BEP 44 items")
            subtitle: qsTr("Immutable items are keyed by the hash of their value; mutable ones by a public key and optional salt, and carry a sequence number and signature.")

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                TableHeader {
                    titles: [
                        { title: qsTr("Kind"), width: 90 },
                        { title: qsTr("Target"), width: 290 },
                        { title: qsTr("Value"), width: 0 },
                        { title: qsTr("Bytes"), width: 60 },
                        { title: qsTr("Seq"), width: 60 },
                        { title: qsTr("Salt"), width: 90 },
                        { title: qsTr("Public key"), width: 130 },
                        { title: qsTr("Expires in"), width: 100 }
                    ]
                }

                ListView {
                    id: itemList

                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: DhtController.storedItems
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: itemRow

                        required property int index
                        required property string kind
                        required property string target
                        required property string value
                        required property string rawValue
                        required property int valueSize
                        required property string sequence
                        required property string salt
                        required property string publicKey
                        required property string expiresIn

                        width: ListView.view.width
                        height: 26
                        color: itemHover.hovered ? Theme.surfaceAlt
                               : itemRow.index % 2 === 0 ? "transparent" : Qt.rgba(1, 1, 1, 0.02)

                        HoverHandler { id: itemHover }
                        ToolTip.visible: itemHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: itemRow.publicKey !== ""
                                      ? qsTr("%1\nbencoded: %2\nsigned by %3").arg(itemRow.target).arg(itemRow.rawValue).arg(itemRow.publicKey)
                                      : qsTr("%1\nbencoded: %2").arg(itemRow.target).arg(itemRow.rawValue)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            spacing: Theme.spacingSmall

                            Cell {
                                cellWidth: 90
                                text: itemRow.kind
                                color: itemRow.kind === "mutable" ? Theme.accent : Theme.textDim
                            }
                            RowLayout {
                                Layout.preferredWidth: 290
                                spacing: 2
                                Cell { text: itemRow.target; font.family: Theme.monoFamily }
                                CopyButton {
                                    value: itemRow.target
                                    what: qsTr("target")
                                }
                            }
                            Cell { text: itemRow.value; font.family: Theme.monoFamily }
                            Cell { cellWidth: 60; text: String(itemRow.valueSize); color: Theme.textDim }
                            Cell { cellWidth: 60; text: itemRow.sequence; color: Theme.textDim }
                            Cell { cellWidth: 90; text: itemRow.salt; color: Theme.textDim; font.family: Theme.monoFamily }
                            // Mutable items only. A key is 64 hex digits, too wide for
                            // the column, so it shows the start and the button copies
                            // the whole key.
                            RowLayout {
                                Layout.preferredWidth: 130
                                spacing: 2
                                Cell {
                                    text: itemRow.publicKey !== "" ? itemRow.publicKey.slice(0, 12) + "\u2026" : "\u2014"
                                    color: Theme.textDim
                                    font.family: Theme.monoFamily
                                }
                                CopyButton {
                                    visible: itemRow.publicKey !== ""
                                    value: itemRow.publicKey
                                    what: qsTr("public key")
                                }
                            }
                            Cell {
                                cellWidth: 100
                                text: itemRow.expiresIn
                                color: itemRow.expiresIn === "expired" ? Theme.bad : Theme.textDim
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacingLarge * 2
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: itemList.count === 0
                        text: page.running ? qsTr("No BEP 44 items stored. Other nodes put them here when this node is among the closest to the item's target.")
                                           : qsTr("Engine stopped")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Peers")
            subtitle: page.selected !== "" ? qsTr("Stored for %1").arg(page.selected)
                                           : qsTr("Select an infohash above.")

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                TableHeader {
                    titles: [
                        { title: qsTr("Address"), width: 0 },
                        { title: qsTr("Family"), width: 70 },
                        { title: qsTr("Announced"), width: 110 },
                        { title: qsTr("Expires in"), width: 100 }
                    ]
                }

                ListView {
                    id: peerList

                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: DhtController.storedPeers
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        id: peerRow

                        required property int index
                        required property string address
                        required property string family
                        required property string announced
                        required property string expiresIn

                        width: ListView.view.width
                        height: 26
                        color: peerRow.index % 2 === 0 ? "transparent" : Qt.rgba(1, 1, 1, 0.02)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            spacing: Theme.spacingSmall

                            Cell { text: peerRow.address; font.family: Theme.monoFamily }
                            Cell { cellWidth: 70; text: peerRow.family; color: Theme.textDim }
                            Cell { cellWidth: 110; text: peerRow.announced; color: Theme.textDim }
                            Cell {
                                cellWidth: 100
                                text: peerRow.expiresIn
                                color: peerRow.expiresIn === "expired" ? Theme.bad : Theme.textDim
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: peerList.count === 0
                        text: page.selected !== "" ? qsTr("No peers stored for this infohash")
                                                   : qsTr("No infohash selected")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }
    }
}
