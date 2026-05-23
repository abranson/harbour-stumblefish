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

    function pendingUploadCount() {
        return count("pending") + count("failed")
    }

    function timeText(ms) {
        var value = Number(ms)
        return value > 0 ? Qt.formatDateTime(new Date(value), "yyyy-MM-dd hh:mm:ss") : ""
    }

    RemorsePopup {
        id: remorse
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: stumblefish.reports

        PullDownMenu {
            MenuItem {
                text: "View map"
                enabled: count("total") > 0
                onClicked: pageStack.push(Qt.resolvedUrl("MapPage.qml"))
            }
            MenuItem {
                text: "Clear pending"
                enabled: pendingUploadCount() > 0 && !stumblefish.busy
                onClicked: remorse.execute("Clearing pending reports", function() {
                    stumblefish.clearPendingReports()
                })
            }
            MenuItem {
                text: "Refresh"
                onClicked: stumblefish.refresh()
            }
        }

        header: PageHeader {
            title: "Reports"
        }

        delegate: ListItem {
            contentHeight: Theme.itemSizeMedium
            property var report: typeof modelData !== "undefined" ? modelData : ({})
            property int reportId: field("id", 0)
            property string uploadStatus: field("uploadStatus", "")
            property double timestampMs: field("timestampMs", 0)
            property int wifiCount: field("wifiCount", 0)
            property int cellCount: field("cellCount", 0)
            property int bleCount: field("bleCount", 0)

            function field(name, fallback) {
                if (report && report[name] !== undefined && report[name] !== null) {
                    return report[name]
                }
                if (typeof model !== "undefined" && model[name] !== undefined && model[name] !== null) {
                    return model[name]
                }
                return fallback
            }

            onClicked: pageStack.push(Qt.resolvedUrl("ReportDetailPage.qml"), { reportId: reportId })

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                }

                Label {
                    width: parent.width
                    text: "#" + reportId + "  " + uploadStatus
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }

                Label {
                    width: parent.width
                    text: timeText(timestampMs)
                          + (timeText(timestampMs).length > 0 ? "  " : "")
                          + wifiCount + " Wi-Fi, " + cellCount
                          + " cell, " + bleCount + " BLE"
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    truncationMode: TruncationMode.Fade
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: "No reports to upload"
        }

        VerticalScrollDecorator {}
    }
}
