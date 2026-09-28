pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// About (app.md 17): name, version, description, build facts (Qt version, USB backend), license
// with `License…`, credits and the non-affiliation notice. Opened from the palette, Home >
// Resources, the help drawer menu and Preferences (all through Launcher.showAbout()).
Dialog {
    id: dialog

    property bool showLicense: false

    anchors.centerIn: Overlay.overlay
    width: 500
    modal: true
    padding: Theme.spacingXl
    standardButtons: Dialog.Close

    onClosed: showLicense = false

    Connections {
        target: Launcher
        function onAboutRequested() {
            dialog.open()
        }
    }

    component InfoLabel: Text {
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeM
    }
    component InfoValue: Text {
        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: Theme.fontSizeM
        elide: Text.ElideRight
    }

    contentItem: ColumnLayout {
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
                    font.pixelSize: Theme.fontSizeHeadline
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                }
                Text {
                    text: qsTr("Version %1").arg(AppInfo.version)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeBody
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Open-source instruments for the NI myDAQ: multimeter, oscilloscope, waveform generator, Bode and spectrum analyzers, digital I/O and counter/timer, in one window.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: facts.implicitHeight + 2 * Theme.spacingM
            radius: Theme.radiusL
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border
            GridLayout {
                id: facts
                x: Theme.spacingM
                y: Theme.spacingM
                width: parent.width - 2 * Theme.spacingM
                columns: 2
                columnSpacing: Theme.spacingL
                rowSpacing: 6
                InfoLabel { text: qsTr("Qt") }
                InfoValue { text: AppInfo.qtVersion; font.family: Theme.monoFamily }
                InfoLabel { text: qsTr("USB backend") }
                InfoValue {
                    text: AppInfo.usbBackend ? qsTr("built in (libusb): real myDAQ and simulator")
                                             : qsTr("not built: simulator only")
                    color: AppInfo.usbBackend ? Theme.text : Theme.warning
                }
                InfoLabel { text: qsTr("Platform") }
                InfoValue { text: ({ macos: "macOS", windows: "Windows", linux: "Linux" })[AppInfo.platform] ?? AppInfo.platform }
                InfoLabel { text: qsTr("License") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    InfoValue {
                        text: {
                            const first = AppInfo.licenseText().split("\n")[0] ?? ""
                            const dash = first.indexOf(" - ")
                            return dash >= 0 ? first.slice(dash + 3) : first
                        }
                    }
                    ActionButton {
                        compact: true
                        text: dialog.showLicense ? qsTr("Hide") : qsTr("License…")
                        onClicked: dialog.showLicense = !dialog.showLicense
                    }
                }
            }
        }

        ScrollView {
            id: licenseView
            visible: dialog.showLicense
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            TextArea {
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                // Hard-wrapped paragraphs of the file reflowed to the dialog width.
                text: AppInfo.licenseText().replace(/([^\n])\n(?!\n)/g, "$1 ")
                font.pixelSize: Theme.fontSizeM
                color: Theme.textSecondary
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: qsTr("Credits")
                color: Theme.text
                font.pixelSize: Theme.fontSizeM
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Written by the %1 contributors. Built with Qt (LGPL-3.0) and libusb (LGPL-2.1). The myDAQ protocol was learned for interoperability; no NI software is included.").arg(AppInfo.name)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeM
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("%1 is an independent project, not affiliated with or endorsed by NI. NI, myDAQ, ELVIS and LabVIEW are trademarks of their owner.").arg(AppInfo.name)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
            wrapMode: Text.WordWrap
        }

        ActionButton {
            tone: "ghost"
            iconName: "external"
            text: qsTr("Project web site")
            toolTip: AppInfo.homepage
            onClicked: Launcher.openUrl(AppInfo.homepage)
        }
    }
}
