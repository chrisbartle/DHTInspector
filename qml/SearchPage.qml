pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: page

    readonly property bool running: DhtController.running
    readonly property int labelWidth: 150

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

    component ResultRow: RowLayout {
        id: resultRow

        property string label
        property string value
        property bool mono: true

        Layout.fillWidth: true
        spacing: Theme.spacing

        FieldLabel { text: resultRow.label }

        Label {
            Layout.fillWidth: true
            text: resultRow.value
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
            font.family: resultRow.mono ? Theme.monoFamily : Qt.application.font.family
            elide: Text.ElideRight
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingLarge

        // --- Lookup ----------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Lookup")
            subtitle: qsTr("A 20-byte hash is either a torrent infohash, searched with get_peers, or a BEP 44 target, fetched with get. Both walk the DHT the same way, asking ever closer nodes until none are nearer.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Hash") }

                ThemedTextField {
                    id: hashField

                    readonly property string validation: DhtController.validateHash(text)

                    Layout.fillWidth: true
                    Accessible.name: qsTr("Hash to look up")
                    enabled: page.running && !DhtController.searchBusy
                    placeholderText: qsTr("40 hexadecimal digits")
                    invalid: validation !== ""
                }

                ThemedButton {
                    text: qsTr("Find peers")
                    primary: true
                    enabled: page.running && !DhtController.searchBusy && hashField.text.trim().length > 0
                             && hashField.validation === ""
                    onClicked: DhtController.searchPeers(hashField.text)
                }

                ThemedButton {
                    text: qsTr("Fetch item")
                    enabled: page.running && !DhtController.searchBusy && hashField.text.trim().length > 0
                             && hashField.validation === ""
                    onClicked: DhtController.searchItem(hashField.text, saltField.text)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Salt") }

                ThemedTextField {
                    id: saltField
                    Layout.preferredWidth: 200
                    Accessible.name: qsTr("Salt")
                    enabled: page.running && !DhtController.searchBusy
                    placeholderText: qsTr("optional")
                }

                Hint { text: qsTr("Only for mutable items, which are stored under the hash of a public key and this salt.") }
            }

            Label {
                Layout.fillWidth: true
                visible: hashField.validation !== "" || !page.running
                text: !page.running ? qsTr("Start the engine on the Setup tab to search.") : hashField.validation
                color: !page.running ? Theme.textFaint : Theme.bad
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                Layout.fillWidth: true
                text: DhtController.searchStatus
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                visible: text !== ""
            }
        }

        // --- Peers -------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Peers")
            subtitle: DhtController.peerSearch.done
                      ? qsTr("%1 peers from %2 of %3 nodes that answered")
                        .arg(DhtController.peerResults.length)
                        .arg(DhtController.peerSearch.responded)
                        .arg(DhtController.peerSearch.queried)
                      : qsTr("Addresses announced for the infohash.")

            ListView {
                id: peerList

                Layout.fillWidth: true
                Layout.preferredHeight: 150
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: DhtController.peerResults
                ScrollBar.vertical: ScrollBar {}

                delegate: Label {
                    required property string modelData
                    required property int index

                    width: ListView.view.width
                    height: 24
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                    text: modelData
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                }

                Label {
                    anchors.centerIn: parent
                    visible: peerList.count === 0
                    text: DhtController.peerSearch.done ? qsTr("No peers announced for that infohash")
                                                        : qsTr("No search run yet")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        // --- Item ---------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("BEP 44 item")
            subtitle: !DhtController.itemSearch.done
                      ? qsTr("Data stored in the DHT under this target.")
                      : DhtController.itemSearch.found
                        ? qsTr("Found by %1 of %2 nodes that answered. A mutable item is only accepted when its signature checks out.")
                          .arg(DhtController.itemSearch.responded).arg(DhtController.itemSearch.queried)
                        : qsTr("Nothing stored under that target, from %1 nodes that answered.")
                          .arg(DhtController.itemSearch.responded)

            ResultRow { label: qsTr("Kind"); value: DhtController.itemSearch.isMutable ? qsTr("mutable") : qsTr("immutable"); visible: DhtController.itemSearch.found; mono: false }
            ResultRow { label: qsTr("Target"); value: DhtController.itemSearch.target; visible: DhtController.itemSearch.target !== "" }
            ResultRow { label: qsTr("Value"); value: DhtController.itemSearch.value; visible: DhtController.itemSearch.found }
            ResultRow { label: qsTr("Bencoded"); value: DhtController.itemSearch.rawValue; visible: DhtController.itemSearch.found }
            ResultRow { label: qsTr("Sequence"); value: DhtController.itemSearch.sequence; visible: DhtController.itemSearch.isMutable }
            ResultRow { label: qsTr("Public key"); value: DhtController.itemSearch.publicKey; visible: DhtController.itemSearch.isMutable }
            ResultRow { label: qsTr("Signature"); value: DhtController.itemSearch.signature; visible: DhtController.itemSearch.isMutable }

            Label {
                Layout.fillWidth: true
                visible: !DhtController.itemSearch.found
                text: qsTr("No item loaded.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        // --- Announce ------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Announce a peer")
            subtitle: qsTr("Tells the closest nodes that a peer for this infohash is reachable. The DHT always records the address the announce came from, so the address published is this node's own; only the port is yours to choose.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Infohash") }

                ThemedTextField {
                    id: announceHash

                    readonly property string validation: DhtController.validateHash(text)

                    Layout.fillWidth: true
                    Accessible.name: qsTr("Infohash to announce")
                    enabled: page.running && !DhtController.publishBusy
                    placeholderText: qsTr("40 hexadecimal digits")
                    invalid: validation !== ""
                }

                ThemedTextField {
                    id: announcePort
                    Layout.preferredWidth: 90
                    Accessible.name: qsTr("Peer port")
                    enabled: page.running && !DhtController.publishBusy && !impliedPort.checked
                    text: "6881"
                    validator: IntValidator { bottom: 1; top: 65535 }
                    invalid: !acceptableInput
                }

                ThemedButton {
                    text: qsTr("Announce")
                    primary: true
                    enabled: page.running && !DhtController.publishBusy && announceHash.validation === ""
                             && announceHash.text.trim().length > 0 && (impliedPort.checked || announcePort.acceptableInput)
                    onClicked: DhtController.announcePeer(announceHash.text, parseInt(announcePort.text), impliedPort.checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Implied port") }

                ThemedSwitch {
                    id: impliedPort
                    Accessible.name: qsTr("Use the port this node listens on")
                    enabled: page.running && !DhtController.publishBusy
                }

                Hint { text: qsTr("Publishes the port this node is listening on, which is what a client behind NAT should do.") }
            }
        }

        // --- Publish -----------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            title: qsTr("Publish a BEP 44 item")
            subtitle: qsTr("An immutable item is stored under the hash of its value and can never change. A mutable one is stored under the hash of a public key and salt, and can be replaced by a later sequence number signed with the same key.")

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Value") }

                ThemedTextField {
                    id: valueField
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Value to publish")
                    enabled: page.running && !DhtController.publishBusy
                    placeholderText: qsTr("stored as a bencoded string, up to 1000 bytes")
                    font.family: Qt.application.font.family
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Immutable target") }

                Label {
                    Layout.fillWidth: true
                    text: valueField.text.length > 0 ? DhtController.immutableTargetFor(valueField.text)
                                                     : qsTr("— enter a value —")
                    color: valueField.text.length > 0 ? Theme.text : Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                    elide: Text.ElideRight
                }

                ThemedButton {
                    text: qsTr("Publish immutable")
                    enabled: page.running && !DhtController.publishBusy && valueField.text.length > 0
                    onClicked: DhtController.publishImmutable(valueField.text)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.border
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Public key") }

                ThemedTextField {
                    id: publicKeyField
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Public key")
                    enabled: page.running && !DhtController.publishBusy
                    placeholderText: qsTr("64 hexadecimal digits")
                }

                ThemedButton {
                    text: qsTr("Generate key pair")
                    enabled: !DhtController.publishBusy
                    onClicked: {
                        const keys = DhtController.generateKeyPair()
                        publicKeyField.text = keys.publicKey
                        secretKeyField.text = keys.secretKey
                        sequenceField.text = "1"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Secret key") }

                ThemedTextField {
                    id: secretKeyField
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Secret key")
                    enabled: page.running && !DhtController.publishBusy
                    placeholderText: qsTr("128 hexadecimal digits, kept only in memory")
                    echoMode: TextInput.Normal
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                FieldLabel { text: qsTr("Salt and sequence") }

                ThemedTextField {
                    id: publishSalt
                    Layout.preferredWidth: 160
                    Accessible.name: qsTr("Salt to publish under")
                    enabled: page.running && !DhtController.publishBusy
                    placeholderText: qsTr("optional")
                }

                ThemedTextField {
                    id: sequenceField
                    Layout.preferredWidth: 90
                    Accessible.name: qsTr("Sequence number")
                    enabled: page.running && !DhtController.publishBusy
                    text: "1"
                    validator: IntValidator { bottom: 0 }
                    invalid: !acceptableInput
                }

                Label {
                    Layout.fillWidth: true
                    text: publicKeyField.text.length > 0
                          ? DhtController.mutableTargetFor(publicKeyField.text, publishSalt.text)
                          : qsTr("— target appears once a key is set —")
                    color: publicKeyField.text.length > 0 ? Theme.text : Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    font.family: Theme.monoFamily
                    elide: Text.ElideRight
                }

                ThemedButton {
                    text: qsTr("Publish mutable")
                    enabled: page.running && !DhtController.publishBusy && valueField.text.length > 0
                             && publicKeyField.text.length > 0 && secretKeyField.text.length > 0
                             && sequenceField.acceptableInput
                    onClicked: DhtController.publishMutable(publicKeyField.text, secretKeyField.text,
                                                            publishSalt.text, parseInt(sequenceField.text),
                                                            valueField.text)
                }
            }

            Label {
                Layout.fillWidth: true
                visible: DhtController.publishStatus.done || DhtController.publishStatus.error !== ""
                text: DhtController.publishStatus.error !== ""
                      ? DhtController.publishStatus.error
                      : qsTr("%1: stored by %2 of %3 nodes, under %4")
                        .arg(DhtController.publishStatus.kind)
                        .arg(DhtController.publishStatus.accepted)
                        .arg(DhtController.publishStatus.attempted)
                        .arg(DhtController.publishStatus.target)
                color: DhtController.publishStatus.error !== "" ? Theme.bad : Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
