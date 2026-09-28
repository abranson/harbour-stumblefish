// SPDX-License-Identifier: MIT
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

    function bleAvailable() {
        return stumblefish.status.bleAvailable === undefined
                || stumblefish.status.bleAvailable === null
                || !!stumblefish.status.bleAvailable
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Upload pending")
                onClicked: stumblefish.uploadPending()
            }
            MenuItem {
                text: qsTr("Refresh")
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
                text: qsTr("Collection")
            }

            DetailItem {
                label: qsTr("Status")
                value: stumblefish.status.collectionStateMessage
            }

            DetailItem {
                label: qsTr("Location")
                value: stumblefish.status.locationEnabled ? "enabled" : "disabled"
            }

            DetailItem {
                label: qsTr("Cell")
                value: stumblefish.status.cellAvailable
                       ? (stumblefish.status.cellStatus || qsTr("available"))
                       : (stumblefish.status.cellUnavailableReason || qsTr("unavailable"))
            }

            DetailItem {
                label: qsTr("Position")
                value: stumblefish.status.positionStatus || qsTr("unknown")
            }

            DetailItem {
                label: qsTr("Fix")
                value: stumblefish.status.hasFix
                       ? stumblefish.status.latitude.toFixed(5) + ", "
                         + stumblefish.status.longitude.toFixed(5)
                         + " ±" + Math.round(stumblefish.status.accuracy) + " m"
                       : "none"
            }

            DetailItem {
                label: "GNSS"
                value: stumblefish.status.gnssBackedFix
                       ? (stumblefish.status.satellitesInUse > 0
                          ? "backed by " + stumblefish.status.satellitesInUse + " satellites"
                          : "backed by GNSS")
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
                        text: qsTr("Wi-Fi")
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
                        text: qsTr("Cell")
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
                    opacity: bleAvailable() ? 1.0 : Theme.opacityLow

                    IconButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        enabled: bleAvailable()
                        icon.source: "image://theme/icon-m-bluetooth"
                        icon.highlighted: !!stumblefish.settings.bleEnabled && bleAvailable()
                        onClicked: stumblefish.setSourceEnabled("ble", !stumblefish.settings.bleEnabled)
                    }

                    Label {
                        width: parent.width
                        text: "BLE"
                        horizontalAlignment: Text.AlignHCenter
                        color: sourceLabelColor(!!stumblefish.settings.bleEnabled, bleAvailable())
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            SectionHeader {
                text: qsTr("Reports")
            }

            DetailItem {
                label: qsTr("Pending")
                value: count("pending")
            }
            DetailItem {
                label: qsTr("Uploaded")
                value: count("uploaded")
            }
            DetailItem {
                label: qsTr("Failed")
                value: count("failed")
            }
            DetailItem {
                label: qsTr("Last report")
                value: timeText(stumblefish.status.lastCollectedMs)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("View reports")
                onClicked: pageStack.push(Qt.resolvedUrl("ReportsPage.qml"))
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("View map")
                enabled: count("total") > 0
                onClicked: pageStack.push(Qt.resolvedUrl("MapPage.qml"))
            }
        }
    }
}
