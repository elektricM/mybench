pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The content of the About window: name, version, what MYBENCH is, build facts, the license
// (expandable), credits and the non-affiliation notice.
Rectangle {
    id: pane

    property bool showLicense: false

    implicitWidth: 480
    implicitHeight: column.implicitHeight + 2 * Theme.spacingXl
    color: Theme.window

    component Label: Text {
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
    }
    component Value: Text {
        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: Theme.fontSizeSmall
        elide: Text.ElideRight
    }

    ColumnLayout {
        id: column
        x: Theme.spacingXl
        y: Theme.spacingXl
        width: pane.width - 2 * Theme.spacingXl
        spacing: Theme.spacingL

        RowLayout {
            spacing: Theme.spacingM
            Image {
                source: Theme.icon("app-icon-color")
                sourceSize: Qt.size(56, 56)
            }
            ColumnLayout {
                spacing: 2
                Text {
                    text: AppInfo.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Version %1").arg(AppInfo.version)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Open-source instruments for the NI myDAQ: multimeter, oscilloscope, spectrum and Bode analyzers, waveform generator, digital I/O, counter/timer, data logger and audio equalizer, in one window.")
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingL
            rowSpacing: Theme.spacingXs + 2
            Label { text: qsTr("Qt") }
            Value { text: AppInfo.qtVersion; font.features: Theme.tabularFigures }
            Label { text: qsTr("USB backend") }
            Value {
                text: AppInfo.usbBackend ? qsTr("Built in (libusb): the myDAQ and the simulator")
                                         : qsTr("Not built: simulator only")
                color: AppInfo.usbBackend ? Theme.text : Theme.caution
            }
            Label { text: qsTr("Platform") }
            Value { text: ({ macos: "macOS", windows: "Windows", linux: "Linux" })[AppInfo.platform] ?? AppInfo.platform }
            Label { text: qsTr("License") }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Value {
                    text: {
                        const first = AppInfo.licenseText().split("\n")[0] ?? ""
                        const dash = first.indexOf(" - ")
                        return dash >= 0 ? first.slice(dash + 3) : first
                    }
                }
                Button {
                    flat: true
                    text: pane.showLicense ? qsTr("Hide License") : qsTr("Show License")
                    onClicked: pane.showLicense = !pane.showLicense
                }
            }
        }

        ScrollView {
            visible: pane.showLicense
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            clip: true
            TextArea {
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                // Hard-wrapped paragraphs of the file reflowed to the pane width.
                text: AppInfo.licenseText().replace(/([^\n])\n(?!\n)/g, "$1 ")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Written by the %1 contributors. Built with Qt (LGPL-3.0) and libusb (LGPL-2.1). The myDAQ protocol was learned for interoperability; no NI software is included.").arg(AppInfo.name)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("%1 is an independent project, not affiliated with or endorsed by NI. NI, myDAQ, ELVIS and LabVIEW are trademarks of their owner.").arg(AppInfo.name)
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }
        Button {
            text: qsTr("Project Web Site")
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: AppInfo.homepage
            onClicked: Launcher.openUrl(AppInfo.homepage)
        }
    }
}
