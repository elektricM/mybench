import QtQuick
import QtTest
import MyBench.Plot
import MyBench.Controls

// BenchPlot takes every color role and its font from Theme (docs/UI.md §2 "Display tokens"), and
// displays follow the theme and "Always Use Dark Displays"; TrendView wires its time windows.
Item {
    id: root
    width: 600
    height: 400

    BenchPlot {
        id: plot
        width: 400
        height: 300
        PlotTrace { id: trace }
    }
    TrendView {
        id: trend
        y: 300
        width: 400
        height: 100
        windows: [{ text: "10 s", seconds: 10 }, { text: "1 min", seconds: 60 }]
        windowIndex: 1
    }
    SignalSpy { id: windowSpy; target: trend; signalName: "windowActivated" }
    TraceBuffer { id: samples }
    TrendView {
        id: dataTrend
        y: 300
        x: 400
        width: 200
        height: 100
        windows: [{ text: "10 s", seconds: 10 }, { text: "1 min", seconds: 60 }, { text: "All", seconds: 0 }]
        windowIndex: 1
        buffers: [samples]
    }

    TestCase {
        name: "PlotTheme"
        when: windowShown

        function init() {
            Theme.platformProvided = false
            Theme.alwaysDarkDisplays = false
        }
        function cleanup() {
            Theme.alwaysDarkDisplays = false
            Theme.mode = "system"
        }

        function checkRoles() {
            compare(plot.backgroundColor, Theme.display)
            compare(plot.plotAreaColor, Theme.display)
            compare(plot.gridColor, Theme.plotGridMajor)
            compare(plot.minorGridColor, Theme.plotGridMinor)
            compare(plot.frameColor, Theme.plotFrame)
            compare(plot.textColor, Theme.displayTextSecondary)
            compare(plot.titleColor, Theme.displayText)
            compare(plot.accentColor, Theme.accent)
            compare(plot.seriesColors.length, Theme.seriesColors.length)
            compare(plot.seriesColors[0], Theme.signalCh0)
            compare(plot.traceColor(trace), Theme.signalCh0)
            compare(plot.paletteColor(1), Theme.signalCh1)
            compare(plot.font.pixelSize, Theme.fontSizeCaption)
            compare(plot.colorScheme, Theme.displayDark ? Plot.Dark : Plot.Light)
        }

        // With `buffers`, the time axis starts at the first sample and grows with the data (≥ 10 s)
        // up to the window, then slides (no trace squeezed at the right of an empty axis).
        function test_trendAxisFromData() {
            // changed() of a TraceBuffer is queued (coalesced): wait for the axis.
            const axis = dataTrend.plot.xAxis
            samples.setUniformValues(1000, 1, [1, 2, 3])
            tryCompare(axis, "min", 1000)
            compare(axis.max, 1010)
            samples.setUniformValues(1000, 1, Array.from({ length: 30 }, (_, i) => i))
            tryCompare(axis, "max", 1029)
            compare(axis.min, 1000)
            samples.setUniformValues(1000, 1, Array.from({ length: 100 }, (_, i) => i))
            tryCompare(axis, "max", 1099)
            compare(axis.min, 1039)           // the 1 min window, sliding with the last sample
            dataTrend.windowIndex = 2         // All
            compare(axis.min, 1000)
            compare(axis.max, 1099)
            dataTrend.windowIndex = 0         // 10 s
            compare(axis.max - axis.min, 10)
            dataTrend.following = false
            samples.setUniformValues(2000, 1, [1, 2, 3])
            wait(50)
            compare(axis.max, 1099, "not following: the axis stays")
            dataTrend.following = true
            compare(axis.min, 2000)
            dataTrend.windowIndex = 1
            // A zoom by the user stops following; choosing a window follows again.
            dataTrend.plot.zoomAt(Qt.point(100, 50), 0.5, 1)
            verify(!dataTrend.following)
            dataTrend.plot.timeWindowActivated(1)     // the toolbar's time window
            verify(dataTrend.following)
        }

        function test_followsTheme() {
            Theme.mode = "light"
            checkRoles()
            verify(Theme.luminance(plot.backgroundColor) > 0.8)
            Theme.mode = "dark"
            checkRoles()
            verify(Theme.luminance(plot.backgroundColor) < 0.05)
        }
        function test_alwaysDarkDisplays() {
            Theme.mode = "light"
            Theme.alwaysDarkDisplays = true
            checkRoles()
            compare(plot.colorScheme, Plot.Dark)
            verify(Theme.luminance(plot.backgroundColor) < 0.05)
            verify(Theme.luminance(plot.textColor) > 0.3)
        }
        function test_bare() {
            plot.bare = true
            compare(plot.backgroundColor, Qt.color("transparent"))
            verify(!plot.frameVisible)
            verify(!plot.interactive)
            plot.bare = false
        }
        function test_trendWindows() {
            compare(trend.plot.timeWindows.length, 2)
            compare(trend.plot.timeWindowIndex, 1)
            compare(trend.windowSeconds, 60)
            windowSpy.clear()
            trend.plot.timeWindowActivated(0)
            compare(windowSpy.count, 1)
            compare(windowSpy.signalArguments[0][0], 0)
            verify(trend.plot.clearAvailable)
            compare(trend.plot.toolbarItems.length, 2)
        }
    }
}
