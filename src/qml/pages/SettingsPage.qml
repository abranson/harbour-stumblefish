import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: "Settings"
            }

            TextSwitch {
                text: "Active mode"
                description: checked ? "Requests location fixes" : "Uses existing location fixes only"
                checked: stumblefish.settings.mode !== "passive"
                onClicked: stumblefish.setMode(checked ? "active" : "passive")
            }

            SectionHeader {
                text: "Sources"
            }

            TextSwitch {
                text: "Wi-Fi"
                checked: !!stumblefish.settings.wifiEnabled
                onClicked: stumblefish.setSourceEnabled("wifi", checked)
            }

            TextSwitch {
                text: "Cell towers"
                enabled: !!stumblefish.status.cellAvailable
                description: enabled ? "" : (stumblefish.status.cellUnavailableReason || "No modem or SIM card")
                checked: enabled && !!stumblefish.settings.cellEnabled
                onClicked: {
                    if (enabled) {
                        stumblefish.setSourceEnabled("cell", checked)
                    }
                }
            }

            TextSwitch {
                text: "BLE beacons"
                description: "Disabled by default"
                checked: !!stumblefish.settings.bleEnabled
                onClicked: stumblefish.setSourceEnabled("ble", checked)
            }

            SectionHeader {
                text: "Upload"
            }

            TextSwitch {
                text: "Automatic upload"
                description: "Manual by default"
                checked: !!stumblefish.settings.autoUploadEnabled
                onClicked: stumblefish.setAutoUploadEnabled(checked)
            }

            TextField {
                id: endpoint
                width: parent.width
                label: "Geosubmit endpoint"
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
        }
    }
}
