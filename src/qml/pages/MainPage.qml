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

    function sourcesText() {
        var sources = []
        if (stumblefish.settings.wifiEnabled) {
            sources.push("Wi-Fi")
        }
        if (stumblefish.settings.cellEnabled) {
            sources.push(stumblefish.status.cellAvailable ? "Cell" : "Cell unavailable")
        }
        if (stumblefish.settings.bleEnabled) {
            sources.push("BLE")
        }
        return sources.length > 0 ? sources.join(", ") : "none"
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

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: stumblefish.message || "BeaconDB collector"
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: "Collection"
            }

            DetailItem {
                label: "Mode"
                value: stumblefish.settings.mode || "active"
            }
            DetailItem {
                label: "Position"
                value: stumblefish.status.positionStatus || "unknown"
            }
            DetailItem {
                label: "Location"
                value: stumblefish.status.locationEnabled ? "enabled" : "disabled"
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
            DetailItem {
                label: "Sources"
                value: sourcesText()
            }
            DetailItem {
                label: "Cell"
                value: stumblefish.status.cellAvailable
                       ? (stumblefish.status.cellStatus || "available")
                       : (stumblefish.status.cellUnavailableReason || "unavailable")
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
                text: "Upload pending"
                enabled: !stumblefish.busy && count("pending") + count("failed") > 0
                onClicked: stumblefish.uploadPending()
            }
        }
    }
}
