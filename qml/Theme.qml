pragma Singleton

import QtQuick

QtObject {
    id: theme

    // Surfaces
    readonly property color background:  "#16191d"
    readonly property color surface:     "#1e2228"
    readonly property color surfaceAlt:  "#262b33"
    readonly property color border:      "#333a44"

    // Text
    readonly property color text:        "#e4e7ec"
    readonly property color textDim:     "#98a1ae"
    readonly property color textFaint:   "#69737f"

    // Semantic accents. Reserved for state, not decoration: a diagnostic
    // tool should only use colour where colour means something.
    readonly property color accent:      "#4a9eff"
    readonly property color good:        "#3fb950"
    readonly property color warn:        "#d9a441"
    readonly property color bad:         "#f0523f"

    // Chart series, in this order and never cycled: categorical slots 1-6
    // of the reference data-viz palette, dark steps, checked against the
    // surface colour for colour-vision separation and contrast.
    readonly property var series: ["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#008300"]
    readonly property color chartGrid: "#2c323b"

    // Metrics
    readonly property int spacingSmall:  6
    readonly property int spacing:       12
    readonly property int spacingLarge:  20
    readonly property int radius:        6

    readonly property int fontSizeSmall:  12
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge:  17

    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas"
                                       : Qt.platform.os === "osx"     ? "Menlo"
                                                                      : "monospace"

    // The widest an address can be in the monospace font at the small size:
    // a full IPv6 endpoint. Measured rather than assumed, because the
    // monospace font differs by platform, so a column sized from it never
    // cuts an address short. Real IPv6 addresses often run close to this.
    readonly property TextMetrics addressMetrics: TextMetrics {
        font.family: theme.monoFamily
        font.pixelSize: theme.fontSizeSmall
        text: "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535"
    }
    readonly property int addressWidth: Math.ceil(theme.addressMetrics.advanceWidth) + 2
    // With the copy button every shown address carries beside it.
    readonly property int addressCellWidth: theme.addressWidth + 22

    // A 20-byte hash in hex, measured the same way.
    readonly property TextMetrics hashMetrics: TextMetrics {
        font.family: theme.monoFamily
        font.pixelSize: theme.fontSizeSmall
        text: "ffffffffffffffffffffffffffffffffffffffff"
    }
    readonly property int hashWidth: Math.ceil(theme.hashMetrics.advanceWidth) + 2
    readonly property int hashCellWidth: theme.hashWidth + 22

    // Binary units, like the byte totals: "512 B/s", "1.5 KiB/s", "16 KiB/s".
    function formatRate(bytesPerSecond) {
        const n = Math.max(0, bytesPerSecond)
        if (n < 1024)
            return Math.round(n) + " B/s"
        const scaled = n < 1024 * 1024 ? n / 1024 : n / (1024 * 1024)
        const unit = n < 1024 * 1024 ? " KiB/s" : " MiB/s"
        return (scaled < 10 ? parseFloat(scaled.toFixed(1)) : Math.round(scaled)) + unit
    }

    function nodeStatusColor(status) {
        switch (status) {
        case "good":
        case "responded":
            return good
        case "questionable":
            return warn
        case "bad":
        case "no response":
            return bad
        case "querying":
            return accent
        default:
            return textDim
        }
    }

    function bep42Color(state) {
        switch (state) {
        case "compliant":
            return good
        case "noncompliant":
            return bad
        case "exempt":
            return textDim
        default:
            return textFaint
        }
    }

    function bep42Text(state) {
        switch (state) {
        case "compliant":
            return qsTr("Compliant")
        case "noncompliant":
            return qsTr("Not compliant")
        case "exempt":
            return qsTr("Exempt: local address")
        default:
            return qsTr("Unknown until the external IP is derived")
        }
    }
}
