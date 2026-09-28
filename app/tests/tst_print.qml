import QtQuick
import QtTest
import MyBench.Framework
import MyBench.Controls

// PrintDialog (app.md 10.2): options per destination; output of the print path. With
// MYBENCH_SHOT_DIR set, the dialog, a decorated PNG and a PDF are written there for review.
Item {
    id: host
    width: 900
    height: 700

    // A small "display": dark background, grid, text and a CH0-coloured trace.
    Rectangle {
        id: display
        width: 600
        height: 300
        color: Theme.display
        Repeater {
            model: 9
            Rectangle { required property int index; x: index * 75; width: 1; height: 300; color: "#2A2E35" }
        }
        Text { x: 8; y: 8; text: "CH 0  1 V/div"; color: "#E5E7EB"; font.pixelSize: 14 }
        Repeater {
            model: 120
            Rectangle {
                required property int index
                x: index * 5; y: 150 - 100 * Math.sin(index / 10); width: 5; height: 3; color: Theme.ch0
            }
        }
    }

    PrintDialog { id: dialog; instrumentTitle: "Oscilloscope" }

    TestCase {
        name: "Print"
        when: windowShown
        readonly property string shots: AppInfo.value("shot-dir")

        function test_optionsPerDestination() {
            dialog.destination = "image"
            const image = dialog.options()
            compare(image.destination, "image")
            compare(image.settingsTable, false)       // a PNG stays a plain grab by default
            compare(image.timeStamp, false)
            dialog.destination = "pdf"
            const page = dialog.options()
            compare(page.settingsTable, true)          // pages get the table and time stamp
            compare(page.timeStamp, true)
            compare(page.orientation, "landscape")
        }

        function test_dialogAndOutput() {
            dialog.destination = "pdf"
            dialog.open()
            tryCompare(dialog, "opened", true)
            wait(300)
            const popup = dialog.contentItem.parent
            if (shots.length > 0)
                grabImage(popup).save(shots + "/print-dialog.png")
            dialog.close()
            tryCompare(dialog, "visible", false)

            const source = DataExport.temporaryFilePath("png")
            const grabbed = grabImage(display)
            grabbed.save(source)
            const metadata = { "Timebase": "1 ms/div", "Trigger": "Edge, Chan 0, rising, 0 V", "Channel 0": "AI 0, 1 V/div, 0 V" }
            const options = { inkSaver: true, userName: true, computerName: true, timeStamp: true, settingsTable: true }
            const dir = shots.length > 0 ? shots : source.substring(0, source.lastIndexOf("/"))
            verify(DataExport.writeImage("file://" + dir + "/print-inksaver.png", source, "Oscilloscope", metadata, options))
            verify(DataExport.writePdf("file://" + dir + "/print-portrait.pdf", source, "Oscilloscope", metadata,
                                       Object.assign({ orientation: "portrait" }, options)))
        }
    }
}
