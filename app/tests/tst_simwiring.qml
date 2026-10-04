import QtQuick
import QtTest
import MyBench.Framework
import MyBench.App

// The simulated wiring list (Overview device block, Simulator pane): AO 0 → AI 0 direct,
// AO 0 → RC <corner> → AI 1, AO 1 not connected (core/src/sim/sim_core.cpp).
Item {
    id: host
    width: 400
    height: 200

    SimWiring { id: wiring }

    TestCase {
        name: "SimWiring"
        when: windowShown

        function test_cornerText() {
            compare(wiring.cornerText(1000), "RC 1 kHz")
            compare(wiring.cornerText(1591.5), "RC 1.59 kHz")
            compare(wiring.cornerText(159.15), "RC 159 Hz")
            compare(wiring.cornerText(20), "RC 20 Hz")
        }
        function test_links() {
            const links = wiring.links
            compare(links.length, 3)
            compare([links[0].from, links[0].to, links[0].link], ["AO 0", "AI 0", "direct"])
            compare([links[1].from, links[1].to], ["AO 0", "AI 1"])
            compare(links[1].link, wiring.cornerText(DeviceHub.simulatorRcCutoff))
            compare([links[2].from, links[2].connected], ["AO 1", false])
            wiring.showAudio = true
            compare(wiring.links.length, 4)
            wiring.showAudio = false
            verify(findChild(wiring, "simWire-1") !== null)
        }
    }
}
