// SPDX-License-Identifier: MIT
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string defaultTileUrl: "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
    property bool endpointLoaded: false
    property bool tileUrlLoaded: false
    property bool endpointDirty: false
    property bool tileUrlDirty: false
    property string endpointSavedText
    property string tileUrlSavedText

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

    function hasSetting(name) {
        var settings = stumblefish.settings
        return settings && settings[name] !== undefined && settings[name] !== null
    }

    function settingText(name) {
        return hasSetting(name) ? String(stumblefish.settings[name]) : ""
    }

    function refreshUrlFields() {
        if (hasSetting("endpoint")) {
            endpointLoaded = true
            endpointSavedText = settingText("endpoint")
            if (!endpoint.activeFocus) {
                endpoint.text = endpointSavedText
                endpointDirty = false
            }
        }

        if (hasSetting("mapTileUrlTemplate")) {
            tileUrlLoaded = true
            tileUrlSavedText = settingText("mapTileUrlTemplate")
            if (!tileUrl.activeFocus) {
                tileUrl.text = tileUrlSavedText
                tileUrlDirty = false
            }
        }
    }

    function saveEndpointField(force) {
        var value = endpoint.text.trim()
        if (!force && !endpointLoaded && !endpointDirty) {
            return
        }
        if (endpointLoaded && value === settingText("endpoint")) {
            endpointSavedText = value
            endpointDirty = false
            return
        }
        if (!force && value === endpointSavedText) {
            endpointDirty = false
            return
        }

        endpointLoaded = true
        endpointDirty = false
        endpointSavedText = value
        stumblefish.setEndpoint(value)
    }

    function saveTileUrlField(force) {
        var value = tileUrl.text.trim()
        if (!force && !tileUrlLoaded && !tileUrlDirty) {
            return
        }
        if (tileUrlLoaded && value === settingText("mapTileUrlTemplate")) {
            tileUrlSavedText = value
            tileUrlDirty = false
            return
        }
        if (!force && value === tileUrlSavedText) {
            tileUrlDirty = false
            return
        }

        tileUrlLoaded = true
        tileUrlDirty = false
        tileUrlSavedText = value
        stumblefish.setMapTileUrlTemplate(value)
    }

    function saveUrlFields() {
        saveEndpointField()
        saveTileUrlField()
    }

    onStatusChanged: {
        if (status === PageStatus.Deactivating) {
            saveUrlFields()
        }
    }

    Component.onCompleted: refreshUrlFields()

    Connections {
        target: stumblefish
        onSettingsChanged: refreshUrlFields()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("Daemon")
            }

            TextSwitch {
                text: qsTr("Allow background collection")
                description: checked
                             ? qsTr("Keeps the collector daemon running after Stumblefish closes")
                             : qsTr("Stops the collector daemon when Stumblefish closes")
                checked: !!stumblefish.settings.allowBackgroundDaemon
                onClicked: stumblefish.setAllowBackgroundDaemon(checked)
            }

            TextSwitch {
                text: qsTr("Status notifications")
                description: checked
                             ? qsTr("Shows collection status while active")
                             : qsTr("Hides collection status notifications")
                checked: !hasSetting("statusNotificationsEnabled")
                         || !!stumblefish.settings.statusNotificationsEnabled
                onClicked: stumblefish.setStatusNotificationsEnabled(checked)
            }

            TextSwitch {
                text: qsTr("Active mode when closed")
                description: checked
                             ? qsTr("Requests location fixes while running in background")
                             : qsTr("Uses other apps' fixes while running in background")
                checked: stumblefish.settings.mode !== "passive"
                onClicked: stumblefish.setMode(checked ? "active" : "passive")
            }

            TextSwitch {
                text: qsTr("Pause active mode on low battery")
                description: checked
                             ? qsTr("Stops active background fixes below 20% unless plugged in")
                             : qsTr("Keeps active background fixes running below 20%")
                checked: !hasSetting("pauseActiveBackgroundOnLowBattery")
                         || !!stumblefish.settings.pauseActiveBackgroundOnLowBattery
                onClicked: stumblefish.setPauseActiveBackgroundOnLowBattery(checked)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Permanent location fixes will drain your battery much faster that usual.")
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: qsTr("Upload")
            }

            TextSwitch {
                text: qsTr("Automatic upload")
                description: qsTr("Every 8 hours")
                checked: !!stumblefish.settings.autoUploadEnabled
                onClicked: stumblefish.setAutoUploadEnabled(checked)
            }

            TextSwitch {
                text: qsTr("Upload when not on Wi-Fi")
                description: qsTr("Applies to automatic uploads")
                checked: !!stumblefish.settings.uploadOnNonWifi
                onClicked: stumblefish.setUploadOnNonWifi(checked)
            }

            TextField {
                id: endpoint
                width: parent.width
                label: "Submission endpoint"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    page.saveEndpointField()
                    focus = false
                }
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        page.saveEndpointField()
                    }
                }
                onTextChanged: {
                    if (activeFocus) {
                        page.endpointDirty = true
                    }
                }
            }

            SectionHeader {
                text: qsTr("Map")
            }

            TextField {
                id: tileUrl
                width: parent.width
                label: qsTr("Tile URL template")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    page.saveTileUrlField()
                    focus = false
                }
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        page.saveTileUrlField()
                    }
                }
                onTextChanged: {
                    if (activeFocus) {
                        page.tileUrlDirty = true
                    }
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Use OSM tiles")
                onClicked: {
                    tileUrl.text = defaultTileUrl
                    page.saveTileUrlField(true)
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Disable map tiles")
                onClicked: {
                    tileUrl.text = ""
                    page.saveTileUrlField(true)
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: qsTr("Map tiles are fetched from the configured provider and may reveal viewed map areas to that provider.")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: qsTr("Storage")
            }

            ComboBox {
                width: parent.width
                label: qsTr("Delete reports older than")
                currentIndex: retentionIndex(stumblefish.settings.reportRetentionDays === undefined
                                             ? 60 : stumblefish.settings.reportRetentionDays)

                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("30 days")
                        onClicked: stumblefish.setReportRetentionDays(30)
                    }
                    MenuItem {
                        text: qsTr("60 days")
                        onClicked: stumblefish.setReportRetentionDays(60)
                    }
                    MenuItem {
                        text: qsTr("180 days")
                        onClicked: stumblefish.setReportRetentionDays(180)
                    }
                    MenuItem {
                        text: qsTr("Never")
                        onClicked: stumblefish.setReportRetentionDays(-1)
                    }
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Prune old reports now")
                enabled: !stumblefish.busy
                onClicked: stumblefish.pruneReports()
            }
        }
    }
}
