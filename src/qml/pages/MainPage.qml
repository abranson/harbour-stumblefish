import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    function count(name) {
        var counts = stumblefish.status.counts
        if (counts && counts[name] !== undefined && counts[name] !== null) {
            return counts[name]
        }
        return 0
    }

    function timeText(ms) {
        var value = Number(ms)
        return value > 0 ? Qt.formatDateTime(new Date(value), "yyyy-MM-dd hh:mm:ss") : "never"
    }

    function sourceLabelColor(enabled, available) {
        if (!available) {
            return Theme.secondaryColor
        }
        return enabled ? Theme.highlightColor : Theme.primaryColor
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: "Settings"
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: "Upload pending"
                onClicked: stumblefish.uploadPending()
            }
            MenuItem {
                text: "Collect now"
                onClicked: stumblefish.collectNow()
            }
            MenuItem {
                text: "Refresh"
                onClicked: stumblefish.refresh()
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Stumblefish"
            }

            SectionHeader {
                text: "Collection"
            }

            DetailItem {
                label: "Status"
                value: stumblefish.status.collectionStateMessage
            }

            DetailItem {
                label: "Location"
                value: stumblefish.status.locationEnabled ? "enabled" : "disabled"
            }

            DetailItem {
                label: "Cell"
                value: stumblefish.status.cellAvailable
                       ? (stumblefish.status.cellStatus || "available")
                       : (stumblefish.status.cellUnavailableReason || "unavailable")
            }

            DetailItem {
                label: "Position"
                value: stumblefish.status.positionStatus || "unknown"
            }

            DetailItem {
                label: "Fix"
                value: stumblefish.status.hasFix
                       ? stumblefish.status.latitude.toFixed(5) + ", "
                         + stumblefish.status.longitude.toFixed(5)
                         + " ±" + Math.round(stumblefish.status.accuracy) + " m"
                       : "none"
            }

            DetailItem {
                label: "GNSS"
                value: stumblefish.status.gnssBackedFix
                       ? "backed by " + stumblefish.status.satellitesInUse + " satellites"
                       : "waiting for satellites"
            }

            Row {
                id: sourceRow
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Math.max(wifiSource.implicitHeight,
                                 cellSource.implicitHeight,
                                 bleSource.implicitHeight)

                Column {
                    id: wifiSource
                    width: parent.width / 3
                    spacing: Theme.paddingSmall

                    IconButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        icon.source: "image://theme/icon-m-wlan"
                        icon.highlighted: !!stumblefish.settings.wifiEnabled
                        onClicked: stumblefish.setSourceEnabled("wifi", !stumblefish.settings.wifiEnabled)
                    }

                    Label {
                        width: parent.width
                        text: "Wi-Fi"
                        horizontalAlignment: Text.AlignHCenter
                        color: sourceLabelColor(!!stumblefish.settings.wifiEnabled, true)
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }

                Column {
                    id: cellSource
                    width: parent.width / 3
                    spacing: Theme.paddingSmall
                    opacity: stumblefish.status.cellAvailable ? 1.0 : Theme.opacityLow

                    IconButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        enabled: !!stumblefish.status.cellAvailable
                        icon.source: "image://theme/icon-m-mobile-network"
                        icon.highlighted: !!stumblefish.settings.cellEnabled && !!stumblefish.status.cellAvailable
                        onClicked: stumblefish.setSourceEnabled("cell", !stumblefish.settings.cellEnabled)
                    }

                    Label {
                        width: parent.width
                        text: "Cell"
                        horizontalAlignment: Text.AlignHCenter
                        color: sourceLabelColor(!!stumblefish.settings.cellEnabled,
                                                !!stumblefish.status.cellAvailable)
                        font.pixelSize: Theme.fontSizeSmall
                        truncationMode: TruncationMode.Fade
                    }
                }

                Column {
                    id: bleSource
                    width: parent.width / 3
                    spacing: Theme.paddingSmall

                    IconButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        icon.source: "image://theme/icon-m-bluetooth"
                        icon.highlighted: !!stumblefish.settings.bleEnabled
                        onClicked: stumblefish.setSourceEnabled("ble", !stumblefish.settings.bleEnabled)
                    }

                    Label {
                        width: parent.width
                        text: "BLE"
                        horizontalAlignment: Text.AlignHCenter
                        color: sourceLabelColor(!!stumblefish.settings.bleEnabled, true)
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            SectionHeader {
                text: "Reports"
            }

            DetailItem {
                label: "Pending"
                value: count("pending")
            }
            DetailItem {
                label: "Uploaded"
                value: count("uploaded")
            }
            DetailItem {
                label: "Failed"
                value: count("failed")
            }
            DetailItem {
                label: "Last report"
                value: timeText(stumblefish.status.lastCollectedMs)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "View reports"
                onClicked: pageStack.push(Qt.resolvedUrl("ReportsPage.qml"))
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "View map"
                onClicked: pageStack.push(Qt.resolvedUrl("MapPage.qml"))
            }
        }
    }
}
