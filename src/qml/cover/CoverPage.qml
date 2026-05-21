// SPDX-License-Identifier: MIT
import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    function pendingCount() {
        var counts = stumblefish.status.counts
        if (counts && counts.pending !== undefined && counts.pending !== null) {
            return counts.pending
        }
        return 0
    }

    Image {
        anchors.centerIn: parent
        width: parent.width * 2.4
        height: width
        source: "../../icons/motorcycle-fish.png"
        fillMode: Image.PreserveAspectFit
        opacity: 0.08
        rotation: -8
        smooth: true
    }

    Column {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            text: "Stumblefish"
            horizontalAlignment: Text.AlignHCenter
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeLarge
        }

        Label {
            width: parent.width
            text: pendingCount() + " pending"
            horizontalAlignment: Text.AlignHCenter
            color: Theme.highlightColor
        }
    }

    CoverActionList {
        CoverAction {
            iconSource: "image://theme/icon-cover-location"
            onTriggered: stumblefish.collectNow()
        }

        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: stumblefish.uploadPending()
        }
    }
}
