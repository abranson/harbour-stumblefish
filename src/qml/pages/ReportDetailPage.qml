import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property int reportId
    property var report: stumblefish.selectedReport.id === reportId ? stumblefish.selectedReport : ({})

    function field(name, fallback) {
        return report && report[name] !== undefined && report[name] !== null ? report[name] : fallback
    }

    function itemField(item, name, fallback) {
        return item && item[name] !== undefined && item[name] !== null ? item[name] : fallback
    }

    function timeText(ms) {
        var value = Number(ms)
        return value > 0 ? Qt.formatDateTime(new Date(value), "yyyy-MM-dd hh:mm:ss") : ""
    }

    function positionText() {
        if (!field("id", 0)) {
            return ""
        }
        return Number(field("latitude", 0)).toFixed(6) + ", "
                + Number(field("longitude", 0)).toFixed(6)
    }

    Component.onCompleted: stumblefish.loadReport(reportId)

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: "Delete report"
                enabled: page.reportId > 0 && !stumblefish.busy
                onClicked: remorse.execute("Deleting report", function() {
                    var id = page.reportId
                    pageStack.pop()
                    stumblefish.deleteReport(id)
                })
            }
            MenuItem {
                text: "Retry upload"
                enabled: page.report.id > 0 && page.report.uploadStatus !== "uploaded"
                onClicked: stumblefish.retryReport(page.report.id)
            }
            MenuItem {
                text: "Refresh"
                onClicked: stumblefish.loadReport(reportId)
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Report " + reportId
            }

            DetailItem {
                label: "Status"
                value: field("uploadStatus", "")
            }
            DetailItem {
                label: "Time"
                value: timeText(field("timestampMs", 0))
            }
            DetailItem {
                label: "Position"
                value: positionText()
            }
            DetailItem {
                label: "Accuracy"
                value: field("id", 0) ? Math.round(Number(field("accuracy", 0))) + " m" : ""
            }
            DetailItem {
                label: "Mode"
                value: field("mode", "")
            }
            DetailItem {
                label: "Endpoint"
                value: field("endpoint", "")
            }
            DetailItem {
                label: "Uploaded"
                value: timeText(field("uploadedAtMs", 0))
                visible: value.length > 0
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: field("lastError", "")
                visible: text.length > 0
                color: Theme.errorColor
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: "Wi-Fi"
            }
            Repeater {
                model: field("wifi", [])
                delegate: DetailItem {
                    label: itemField(modelData, "macAddress", "")
                    value: itemField(modelData, "signalStrength", "")
                }
            }

            SectionHeader {
                text: "Cell Towers"
            }
            Repeater {
                model: field("cells", [])
                delegate: DetailItem {
                    label: itemField(modelData, "radioType", "")
                    value: itemField(modelData, "mobileCountryCode", "")
                           + "-" + itemField(modelData, "mobileNetworkCode", "")
                           + " " + itemField(modelData, "locationAreaCode", "")
                           + "/" + itemField(modelData, "cellId", "")
                }
            }

            SectionHeader {
                text: "BLE Beacons"
            }
            Repeater {
                model: field("ble", [])
                delegate: DetailItem {
                    label: itemField(modelData, "name", "") || itemField(modelData, "macAddress", "")
                    value: itemField(modelData, "macAddress", "")
                           + "  " + itemField(modelData, "signalStrength", "")
                }
            }
        }
    }
}
