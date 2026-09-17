pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A time-series line chart: one y-scale, 2 px lines, a legend for two or
// more series, and a crosshair with a readout on hover. Missing values and
// gaps break the line. Colours come from Theme.series in the order given.
ColumnLayout {
    id: chart

    // Sample times as milliseconds since the epoch, with a flag per sample
    // that marks a gap before it.
    property var times: []
    property var gaps: []
    // [{ name: string, values: [number or null] }], coloured in this order.
    property var series: []
    // Formats a value for the axis and readout.
    property var format: function (v) { return Number(v).toLocaleString(Qt.locale(), "f", 0) }
    // Fixed top of the scale (e.g. 1 for shares), or <= 0 to fit the data.
    property real fixedMax: 0
    property string title
    property bool showTable: false

    readonly property int count: times ? times.length : 0
    readonly property real maxValue: {
        if (fixedMax > 0)
            return fixedMax
        let m = 0
        for (const s of series)
            for (const v of s.values)
                if (v !== null && v !== undefined && v > m)
                    m = v
        return m
    }
    // A round step for four gridlines, and the scale's top on one of them.
    readonly property real step: niceStep(maxValue / 4)
    readonly property real scaleTop: Math.max(step, Math.ceil(maxValue / step) * step)
    property int hoverIndex: -1

    spacing: Theme.spacingSmall

    function niceStep(raw) {
        if (!(raw > 0))
            return 1
        const p = Math.pow(10, Math.floor(Math.log10(raw)))
        for (const m of [1, 2, 2.5, 5, 10])
            if (m * p >= raw)
                return m * p
        return 10 * p
    }

    function colorOf(i) {
        return Theme.series[i % Theme.series.length]
    }

    // Clock times; with seconds when the span is short enough for minutes
    // to repeat, with the date when it spans most of a day.
    function timeLabel(ms, withDate, withSeconds) {
        const d = new Date(ms)
        const time = withSeconds ? "HH:mm:ss" : "HH:mm"
        return withDate ? d.toLocaleString(Qt.locale(), "d MMM " + time) : d.toLocaleTimeString(Qt.locale(), time)
    }

    function valueAt(s, i) {
        const v = s.values[i]
        return v === null || v === undefined ? null : v
    }

    // The latest known value of each series, for screen readers.
    function summary() {
        if (count === 0)
            return qsTr("No samples yet")
        const parts = []
        for (const s of series) {
            for (let i = count - 1; i >= 0; --i) {
                const v = valueAt(s, i)
                if (v !== null) {
                    parts.push(s.name + " " + format(v))
                    break
                }
            }
        }
        return qsTr("%1 samples since %2. Latest: %3").arg(count).arg(timeLabel(times[0], true)).arg(parts.join(", "))
    }

    onSeriesChanged: canvas.requestPaint()
    onTimesChanged: canvas.requestPaint()
    onHoverIndexChanged: canvas.requestPaint()

    // Legend: identity never rests on colour alone.
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacing
        visible: chart.series.length > 1

        Repeater {
            model: chart.series
            delegate: Row {
                id: key
                required property var modelData
                required property int index
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 2
                    radius: 1
                    color: chart.colorOf(key.index)
                }
                Label {
                    text: key.modelData.name
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
    }

    Item {
        id: plotArea

        readonly property int padLeft: 64
        readonly property int padRight: 12
        readonly property int topPad: 8
        readonly property int padBottom: 22
        readonly property real plotWidth: Math.max(1, width - padLeft - padRight)
        readonly property real plotHeight: Math.max(1, height - topPad - padBottom)
        readonly property real t0: chart.count > 0 ? chart.times[0] : 0
        readonly property real t1: chart.count > 0 ? chart.times[chart.count - 1] : 1

        function xOf(i) {
            if (chart.count <= 1)
                return padLeft + plotWidth / 2
            return padLeft + (chart.times[i] - t0) / Math.max(1, t1 - t0) * plotWidth
        }
        function yOf(v) {
            return topPad + plotHeight - Math.min(1, v / chart.scaleTop) * plotHeight
        }

        Layout.fillWidth: true
        Layout.preferredHeight: 220
        visible: chart.count > 0

        Accessible.role: Accessible.Chart
        Accessible.name: chart.title
        Accessible.description: chart.summary()

        onWidthChanged: canvas.requestPaint()
        onHeightChanged: canvas.requestPaint()

        Canvas {
            id: canvas
            anchors.fill: parent

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const p = plotArea

                // Gridlines and the value scale.
                ctx.lineWidth = 1
                ctx.strokeStyle = String(Theme.chartGrid)
                for (let k = 0; k <= 4; ++k) {
                    const v = chart.scaleTop * k / 4
                    const y = Math.round(p.yOf(v)) + 0.5
                    ctx.beginPath()
                    ctx.moveTo(p.padLeft, y)
                    ctx.lineTo(p.padLeft + p.plotWidth, y)
                    ctx.stroke()
                }

                // Lines, broken at gaps and unknown values; a lone sample
                // gets a dot so it still shows.
                ctx.lineWidth = 2
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                for (let s = 0; s < chart.series.length; ++s) {
                    const ser = chart.series[s]
                    ctx.strokeStyle = chart.colorOf(s)
                    ctx.fillStyle = chart.colorOf(s)
                    ctx.beginPath()
                    let open = false
                    for (let i = 0; i < chart.count; ++i) {
                        const v = chart.valueAt(ser, i)
                        if (v === null || (chart.gaps[i] && i > 0)) {
                            open = false
                            if (v === null)
                                continue
                        }
                        const x = p.xOf(i)
                        const y = p.yOf(v)
                        if (!open) {
                            ctx.moveTo(x, y)
                            const next = i + 1 < chart.count ? chart.valueAt(ser, i + 1) : null
                            if (next === null || chart.gaps[i + 1]) {
                                ctx.stroke()
                                ctx.beginPath()
                                ctx.arc(x, y, 3, 0, 2 * Math.PI)
                                ctx.fill()
                                ctx.beginPath()
                                continue
                            }
                            open = true
                        } else {
                            ctx.lineTo(x, y)
                        }
                    }
                    ctx.stroke()
                }

                // Crosshair and markers, ringed in the surface colour.
                const h = chart.hoverIndex
                if (h >= 0 && h < chart.count) {
                    const x = Math.round(p.xOf(h)) + 0.5
                    ctx.lineWidth = 1
                    ctx.strokeStyle = String(Theme.textFaint)
                    ctx.beginPath()
                    ctx.moveTo(x, p.topPad)
                    ctx.lineTo(x, p.topPad + p.plotHeight)
                    ctx.stroke()
                    for (let s = 0; s < chart.series.length; ++s) {
                        const v = chart.valueAt(chart.series[s], h)
                        if (v === null)
                            continue
                        ctx.beginPath()
                        ctx.arc(x, p.yOf(v), 5, 0, 2 * Math.PI)
                        ctx.fillStyle = String(Theme.surface)
                        ctx.fill()
                        ctx.beginPath()
                        ctx.arc(x, p.yOf(v), 4, 0, 2 * Math.PI)
                        ctx.fillStyle = chart.colorOf(s)
                        ctx.fill()
                    }
                }
            }
        }

        // Scale labels.
        Repeater {
            model: 5
            delegate: Label {
                required property int index
                x: 0
                width: plotArea.padLeft - 8
                y: plotArea.yOf(chart.scaleTop * index / 4) - height / 2
                horizontalAlignment: Text.AlignRight
                text: chart.format(chart.scaleTop * index / 4)
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall - 1
            }
        }

        // Time labels, at most five.
        Repeater {
            model: chart.count > 1 ? 5 : (chart.count === 1 ? 1 : 0)
            delegate: Label {
                required property int index
                readonly property real at: chart.count > 1 ? plotArea.t0 + (plotArea.t1 - plotArea.t0) * index / 4 : plotArea.t0
                readonly property real xPos: chart.count > 1 ? plotArea.padLeft + plotArea.plotWidth * index / 4
                                                             : plotArea.padLeft + plotArea.plotWidth / 2
                x: Math.max(0, Math.min(plotArea.width - width, xPos - width / 2))
                y: plotArea.height - height
                text: chart.timeLabel(at, plotArea.t1 - plotArea.t0 > 20 * 3600 * 1000,
                                      plotArea.t1 - plotArea.t0 < 20 * 60 * 1000)
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall - 1
            }
        }

        HoverHandler {
            id: hover
            onPointChanged: {
                if (!hovered || chart.count === 0) {
                    chart.hoverIndex = -1
                    return
                }
                // Nearest sample in time; the hit area is the whole plot.
                const x = point.position.x
                let best = -1
                let bestDistance = Infinity
                for (let i = 0; i < chart.count; ++i) {
                    const d = Math.abs(plotArea.xOf(i) - x)
                    if (d < bestDistance) {
                        bestDistance = d
                        best = i
                    }
                }
                chart.hoverIndex = best
            }
            onHoveredChanged: if (!hovered) chart.hoverIndex = -1
        }

        // Readout.
        Rectangle {
            id: readout
            visible: chart.hoverIndex >= 0
            readonly property real anchorX: chart.hoverIndex >= 0 ? plotArea.xOf(chart.hoverIndex) : 0
            x: anchorX + 12 + width > plotArea.width ? anchorX - 12 - width : anchorX + 12
            y: plotArea.topPad
            width: readoutColumn.implicitWidth + 16
            height: readoutColumn.implicitHeight + 12
            radius: Theme.radius
            color: Theme.background
            border.color: Theme.border

            Column {
                id: readoutColumn
                x: 8
                y: 6
                spacing: 2
                Label {
                    text: chart.hoverIndex >= 0 ? chart.timeLabel(chart.times[chart.hoverIndex], true, true) : ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSmall - 1
                }
                Repeater {
                    model: chart.series
                    delegate: Row {
                        id: line
                        required property var modelData
                        required property int index
                        readonly property var value: chart.hoverIndex >= 0 ? chart.valueAt(modelData, chart.hoverIndex) : null
                        spacing: 6
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8
                            height: 8
                            radius: 4
                            color: chart.colorOf(line.index)
                        }
                        Label {
                            text: line.modelData.name
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Label {
                            text: line.value === null ? "—" : chart.format(line.value)
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSmall
                            font.family: Theme.monoFamily
                        }
                    }
                }
            }
        }
    }

    // The same numbers as a table, newest first.
    GridLayout {
        id: table
        Layout.fillWidth: true
        visible: chart.showTable && chart.count > 0
        columns: chart.series.length + 1
        columnSpacing: Theme.spacingLarge
        rowSpacing: 2

        readonly property int rows: Math.min(chart.count, 40)

        Label {
            text: qsTr("Time")
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
        }
        Repeater {
            model: table.visible ? chart.series : []
            delegate: Label {
                required property var modelData
                Layout.alignment: Qt.AlignRight
                text: modelData.name
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
        }
        Repeater {
            model: table.visible ? table.rows * (chart.series.length + 1) : 0
            delegate: Label {
                required property int index
                readonly property int sample: chart.count - 1 - Math.floor(index / (chart.series.length + 1))
                readonly property int column: index % (chart.series.length + 1)
                readonly property var value: column === 0 ? null : chart.valueAt(chart.series[column - 1], sample)
                Layout.alignment: column === 0 ? Qt.AlignLeft : Qt.AlignRight
                text: column === 0 ? chart.timeLabel(chart.times[sample], true, true)
                                   : value === null ? "—" : chart.format(value)
                color: column === 0 ? Theme.textDim : Theme.text
                font: column === 0 ? Qt.font({ pixelSize: Theme.fontSizeSmall })
                                   : Qt.font({ family: Theme.monoFamily, pixelSize: Theme.fontSizeSmall })
            }
        }
        Label {
            Layout.columnSpan: table.columns
            visible: chart.count > table.rows
            text: qsTr("The newest %1 of %2 samples; the export has them all.").arg(table.rows).arg(chart.count)
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
        }
    }

    EmptyState {
        visible: chart.count === 0
        message: qsTr("No samples yet. Samples are taken while monitoring is on.")
    }
}
