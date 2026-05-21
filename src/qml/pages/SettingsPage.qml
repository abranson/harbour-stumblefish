// SPDX-License-Identifier: MIT
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string defaultTileUrl: "https://tile.openstreetmap.org/{z}/{x}/{y}.png"

    function retentionIndex(days) {
        var value = Number(days)
        if (value === 30) {
            return 0
        }
        if (value === 180) {
            return 2
        }
        if (value === -1) {
            return 3
        }
        return 1
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: "About"
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Settings"
            }

            SectionHeader {
                text: "Daemon"
            }

            TextSwitch {
                text: "Allow background collection"
                description: checked
                             ? "Keeps the collector daemon running after Stumblefish closes"
                             : "Stops the collector daemon when Stumblefish closes"
                checked: !!stumblefish.settings.allowBackgroundDaemon
                onClicked: stumblefish.setAllowBackgroundDaemon(checked)
            }

            TextSwitch {
                text: "Active mode when closed"
                description: checked
                             ? "Requests location fixes while running in background"
                             : "Uses other apps' fixes while running in background"
                checked: stumblefish.settings.mode !== "passive"
                onClicked: stumblefish.setMode(checked ? "active" : "passive")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: "Permanent location fixes will drain your battery much faster that usual."
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: "Upload"
            }

            TextSwitch {
                text: "Automatic upload"
                description: "Every 8 hours"
                checked: !!stumblefish.settings.autoUploadEnabled
                onClicked: stumblefish.setAutoUploadEnabled(checked)
            }

            TextSwitch {
                text: "Upload when not on Wi-Fi"
                description: "Applies to automatic uploads"
                checked: !!stumblefish.settings.uploadOnNonWifi
                onClicked: stumblefish.setUploadOnNonWifi(checked)
            }

            TextField {
                id: endpoint
                width: parent.width
                label: "Submission endpoint"
                text: stumblefish.settings.endpoint || ""
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    stumblefish.setEndpoint(text)
                    focus = false
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Save endpoint"
                onClicked: stumblefish.setEndpoint(endpoint.text)
            }

            SectionHeader {
                text: "Map"
            }

            TextField {
                id: tileUrl
                width: parent.width
                label: "Tile URL template"
                text: stumblefish.settings.mapTileUrlTemplate || ""
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    stumblefish.setMapTileUrlTemplate(text)
                    focus = false
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Save tile URL"
                onClicked: stumblefish.setMapTileUrlTemplate(tileUrl.text)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Use OSM tiles"
                onClicked: {
                    tileUrl.text = defaultTileUrl
                    stumblefish.setMapTileUrlTemplate(defaultTileUrl)
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Disable map tiles"
                onClicked: {
                    tileUrl.text = ""
                    stumblefish.setMapTileUrlTemplate("")
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: "Map tiles are fetched from the configured provider and may reveal viewed map areas to that provider."
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: "Storage"
            }

            ComboBox {
                width: parent.width
                label: "Delete reports older than"
                currentIndex: retentionIndex(stumblefish.settings.reportRetentionDays === undefined
                                             ? 60 : stumblefish.settings.reportRetentionDays)

                menu: ContextMenu {
                    MenuItem {
                        text: "30 days"
                        onClicked: stumblefish.setReportRetentionDays(30)
                    }
                    MenuItem {
                        text: "60 days"
                        onClicked: stumblefish.setReportRetentionDays(60)
                    }
                    MenuItem {
                        text: "180 days"
                        onClicked: stumblefish.setReportRetentionDays(180)
                    }
                    MenuItem {
                        text: "Never"
                        onClicked: stumblefish.setReportRetentionDays(-1)
                    }
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Prune old reports now"
                enabled: !stumblefish.busy
                onClicked: stumblefish.pruneReports()
            }
        }
    }
}
