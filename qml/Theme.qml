pragma Singleton

import QtQuick

QtObject {
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
            return qsTr("BEP 42 compliant")
        case "noncompliant":
            return qsTr("Not BEP 42 compliant")
        case "exempt":
            return qsTr("BEP 42 exempt: local address")
        default:
            return qsTr("BEP 42 unknown: external address not established")
        }
    }
}
