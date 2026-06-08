// SPDX-License-Identifier: MIT
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

    function positiveItemField(item, name) {
        var value = Number(itemField(item, name, -1))
        return value > 0 ? value : ""
    }

    function knownItemField(item, name) {
        var value = Number(itemField(item, name, -1))
        return value >= 0 ? value : ""
    }

    function timeText(ms) {
        var value = Number(ms)
        return value > 0 ? Qt.formatDateTime(new Date(value), "yyyy-MM-dd hh:mm:ss") : ""
    }

    function seenBeforeReportText(item) {
        var reportTimestamp = Number(field("timestampMs", 0))
        var seenTimestamp = Number(itemField(item, "seenMs", 0))
        if (reportTimestamp <= 0 || seenTimestamp <= 0) {
            return ""
        }

        var seconds = Math.round((reportTimestamp - seenTimestamp) / 1000)
        return seconds >= 0 ? seconds + "s before" : ""
    }

    function wifiText(item) {
        var parts = []
        var seen = seenBeforeReportText(item)
        if (seen !== "") {
            parts.push(seen)
        }

        var signalStrength = Number(itemField(item, "signalStrength", 0))
        if (signalStrength < 0) {
            parts.push(signalStrength + " dBm")
        }

        var frequency = positiveItemField(item, "frequency")
        if (frequency !== "") {
            parts.push(frequency + " MHz")
        }

        return parts.join("\n")
    }

    function positionText() {
        if (!field("id", 0)) {
            return ""
        }
        return Number(field("latitude", 0)).toFixed(6) + ", "
                + Number(field("longitude", 0)).toFixed(6)
    }

    function cellRadioType(item) {
        return String(itemField(item, "radioType", "cell"))
    }

    function cellLabel(item) {
        return cellRadioType(item).toUpperCase()
                + (itemField(item, "serving", false) ? " serving" : " neighbour")
    }

    function cellAreaName(item) {
        var radioType = cellRadioType(item)
        return radioType === "lte" || radioType === "nr" ? "TAC" : "LAC"
    }

    function cellCodeName(item) {
        var radioType = cellRadioType(item)
        return radioType === "lte" || radioType === "nr" ? "PCI" : "PSC"
    }

    function cellArfcnName(item) {
        var radioType = cellRadioType(item)
        if (radioType === "lte") {
            return "EARFCN"
        } else if (radioType === "nr") {
            return "NRARFCN"
        } else if (radioType === "wcdma") {
            return "UARFCN"
        }
        return "ARFCN"
    }

    function cellText(item) {
        var parts = []
        var mobileCountryCode = positiveItemField(item, "mobileCountryCode")
        var mobileNetworkCode = knownItemField(item, "mobileNetworkCode")
        if (mobileCountryCode !== "" && mobileNetworkCode !== "") {
            parts.push(mobileCountryCode + "-" + mobileNetworkCode)
        }

        var locationAreaCode = positiveItemField(item, "locationAreaCode")
        if (locationAreaCode !== "") {
            parts.push(cellAreaName(item) + " " + locationAreaCode)
        }

        var cellId = positiveItemField(item, "cellId")
        if (cellId !== "") {
            parts.push("ID " + cellId)
        }

        var primaryScramblingCode = knownItemField(item, "primaryScramblingCode")
        if (primaryScramblingCode !== "") {
            parts.push(cellCodeName(item) + " " + primaryScramblingCode)
        }

        var arfcn = knownItemField(item, "arfcn")
        if (arfcn !== "") {
            parts.push(cellArfcnName(item) + " " + arfcn)
        }

        var asu = knownItemField(item, "asu")
        if (asu !== "") {
            parts.push("ASU " + asu)
        }

        var timingAdvance = knownItemField(item, "timingAdvance")
        if (timingAdvance !== "") {
            parts.push("TA " + timingAdvance)
        }

        var signalStrength = Number(itemField(item, "signalStrength", 0))
        if (signalStrength < 0) {
            parts.push(signalStrength + " dBm")
        }
        return parts.join("  ")
    }

    Component.onCompleted: stumblefish.loadReport(reportId)

    Connections {
        target: stumblefish
        onReportsChanged: stumblefish.loadReport(reportId)
    }

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
                    value: wifiText(modelData)
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: "None found"
                visible: field("id", 0) > 0 && field("wifi", []).length === 0
                color: Theme.secondaryColor
            }

            SectionHeader {
                text: "Cell Towers"
            }
            Repeater {
                model: field("cells", [])
                delegate: DetailItem {
                    label: cellLabel(modelData)
                    value: cellText(modelData)
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: "None found"
                visible: field("id", 0) > 0 && field("cells", []).length === 0
                color: Theme.secondaryColor
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
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: "None found"
                visible: field("id", 0) > 0 && field("ble", []).length === 0
                color: Theme.secondaryColor
            }
        }
    }
}
