import QtQuick
import MyBench.Plot

// MyBench.Plot's Plot with the shell theme: dark/light color scheme from Theme and the display
// surface as background, so it blends into a DisplayArea. Use it instead of Plot in panels and
// tiles; every Plot property, element and method is available (see ui/plot/README.md).
//     BenchPlot { anchors.fill: parent; PlotTrace { buffer: controller.ch0; color: Theme.ch0 } }
//     BenchPlot { bare: true; ... }     // tiles: transparent, no frame/labels/interaction
Plot {
    // Minimal look for tiles and previews: transparent, no frame, no labels, no interaction,
    // no margins (set it, then only add traces).
    property bool bare: false

    colorScheme: Theme.dark ? Plot.Dark : Plot.Light
    backgroundColor: bare ? "transparent" : Theme.display
    plotAreaColor: bare ? "transparent" : undefined
    frameVisible: !bare
    interactive: !bare
    xLabelsVisible: !bare
    yLabelsVisible: !bare
    leftMargin: bare ? 0 : -1
    rightMargin: bare ? 0 : -1
    topMargin: bare ? 2 : -1
    bottomMargin: bare ? 2 : -1
}
