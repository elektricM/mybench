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
