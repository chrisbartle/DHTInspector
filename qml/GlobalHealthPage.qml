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

        GridLayout {
            Layout.fillWidth: true
            columns: page.availableWidth > 900 ? 2 : 1
            columnSpacing: Theme.spacingLarge
            rowSpacing: Theme.spacingLarge

            Panel {
                Layout.fillWidth: true
                title: qsTr("Population Estimate")
                subtitle: qsTr("Derived from the density of the closest node set over many random targets.")
                EmptyState { message: qsTr("No crawl data") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Lookup Performance")
                subtitle: qsTr("Success rate, hop count and latency for iterative lookups.")
                EmptyState { message: qsTr("No lookups run") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Liveness and Churn")
                subtitle: qsTr("What fraction of discovered nodes actually answer, and how fast they turn over.")
                EmptyState { message: qsTr("No samples") }
            }

            Panel {
                Layout.fillWidth: true
                title: qsTr("Client Distribution")
                subtitle: qsTr("Implementation and version mix, read from the 'v' field.")
                EmptyState { message: qsTr("No samples") }
            }
        }

        Panel {
            Layout.fillWidth: true
            title: qsTr("ID Space Coverage")
            subtitle: qsTr("Distribution of observed node IDs across the 160-bit keyspace. Clustering here is how sybil and eclipse activity shows up.")
            EmptyState {
                message: qsTr("No crawl data")
                implicitHeight: 180
            }
        }

    }
}
