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
            title: qsTr("Local Node")
            subtitle: qsTr("The identity and sockets this tool will use once the engine exists.")

            LabeledField { label: qsTr("Node ID"); value: qsTr("— not generated —"); valueColor: Theme.textFaint }
            LabeledField { label: qsTr("External IP (v4)"); value: qsTr("— unknown —"); valueColor: Theme.textFaint }
            LabeledField { label: qsTr("External IP (v6)"); value: qsTr("— unknown —"); valueColor: Theme.textFaint }
            LabeledField { label: qsTr("Listen port"); value: "6881" }
            LabeledField { label: qsTr("Address families"); value: qsTr("IPv4, IPv6") }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Bootstrap Routers")
            subtitle: qsTr("Well-known entry points into the mainline DHT.")

            LabeledField { label: qsTr("Primary"); value: "router.bittorrent.com:6881" }
            LabeledField { label: qsTr("Alternate"); value: "router.utorrent.com:6881" }
            LabeledField { label: qsTr("Alternate"); value: "dht.transmissionbt.com:6881" }
            LabeledField { label: qsTr("Alternate"); value: "dht.libtorrent.org:25401" }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("Network Posture")
            subtitle: qsTr("How much this tool is allowed to affect the live network. Anything that writes state stays off until it is deliberately turned on.")

            LabeledField { label: qsTr("Read-only flag (BEP 43)"); value: qsTr("set"); valueColor: Theme.good }
            LabeledField { label: qsTr("announce_peer"); value: qsTr("disabled"); valueColor: Theme.textFaint }
            LabeledField { label: qsTr("BEP 44 put"); value: qsTr("disabled"); valueColor: Theme.textFaint }
            LabeledField { label: qsTr("Outbound query rate"); value: qsTr("50 / s") }
        }

    }
}
