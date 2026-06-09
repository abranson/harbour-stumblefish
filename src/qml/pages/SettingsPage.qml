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
                text: "Status notifications"
                description: checked
                             ? "Shows collection status while active"
                             : "Hides collection status notifications"
                checked: !hasSetting("statusNotificationsEnabled")
                         || !!stumblefish.settings.statusNotificationsEnabled
                onClicked: stumblefish.setStatusNotificationsEnabled(checked)
            }

            TextSwitch {
                text: "Active mode when closed"
                description: checked
                             ? "Requests location fixes while running in background"
                             : "Uses other apps' fixes while running in background"
                checked: stumblefish.settings.mode !== "passive"
                onClicked: stumblefish.setMode(checked ? "active" : "passive")
            }

            TextSwitch {
                text: "Pause active mode on low battery"
                description: checked
                             ? "Stops active background fixes below 20% unless plugged in"
                             : "Keeps active background fixes running below 20%"
                checked: !hasSetting("pauseActiveBackgroundOnLowBattery")
                         || !!stumblefish.settings.pauseActiveBackgroundOnLowBattery
                onClicked: stumblefish.setPauseActiveBackgroundOnLowBattery(checked)
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
                text: "Map"
            }

            TextField {
                id: tileUrl
                width: parent.width
                label: "Tile URL template"
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
                text: "Use OSM tiles"
                onClicked: {
                    tileUrl.text = defaultTileUrl
                    page.saveTileUrlField(true)
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Disable map tiles"
                onClicked: {
                    tileUrl.text = ""
                    page.saveTileUrlField(true)
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
