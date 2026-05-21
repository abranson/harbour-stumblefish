// SPDX-License-Identifier: MIT
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    backNavigation: !mapInteractionActive

    property int tileSize: 256
    property int minimumZoom: 1
    property int maximumZoom: 19
    property int defaultReportZoom: 14
    property real zoom: defaultReportZoom
    property int tileZoom: defaultReportZoom
    property real centerLatitude: 0
    property real centerLongitude: 0
    property bool initialized: false
    property bool mapInteractionActive: false
    property var topLeftPixel: ({ "x": 0, "y": 0 })
    property var tileModel: []
    property var previousTileModel: []
    property int previousTileZoom: defaultReportZoom
    property bool showPreviousTiles: false
    property int currentTileReadyCount: 0
    property int currentTileTargetCount: 0
    property string currentTileKey
    property string previousTileKey
    property string osmTileUrl: "https://tile.openstreetmap.org/{z}/{x}/{y}.png"

    onZoomChanged: {
        if (zoomAnimation.running) {
            updateMap()
        }
    }

    function worldSize(z) {
        return tileSize * Math.pow(2, Math.max(0, Math.min(20, z)))
    }

    function boundedZoom(value) {
        return Math.max(minimumZoom, Math.min(maximumZoom, value))
    }

    function tileRequestZoom() {
        return Math.max(minimumZoom, Math.min(maximumZoom, Math.round(zoom)))
    }

    function boundedLatitude(latitude) {
        return Math.max(-85.05112878, Math.min(85.05112878, latitude))
    }

    function normalizedLongitude(longitude) {
        var result = longitude
        while (result < -180) {
            result += 360
        }
        while (result > 180) {
            result -= 360
        }
        return result
    }

    function latLonToPixel(latitude, longitude, z) {
        var size = worldSize(z)
        var lat = boundedLatitude(latitude) * Math.PI / 180
        var lon = normalizedLongitude(longitude)
        return {
            "x": (lon + 180) / 360 * size,
            "y": (0.5 - Math.log((1 + Math.sin(lat)) / (1 - Math.sin(lat))) / (4 * Math.PI)) * size
        }
    }

    function pixelToLatLon(x, y, z) {
        var size = worldSize(z)
        var lon = normalizedLongitude(x / size * 360 - 180)
        var n = Math.PI - 2 * Math.PI * y / size
        var lat = 180 / Math.PI * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)))
        return { "latitude": boundedLatitude(lat), "longitude": lon }
    }

    function mapSummaryValue(name, fallback) {
        var summary = stumblefish.mapSummary
        if (summary && summary[name] !== undefined && summary[name] !== null) {
            return summary[name]
        }
        return fallback
    }

    function reportCount() {
        return Number(mapSummaryValue("total", 0))
    }

    function tileTemplate() {
        var configured = stumblefish.settings.mapTileUrlTemplate
        return configured === undefined || configured === null
                ? osmTileUrl : String(configured)
    }

    function isOsmTiles() {
        return tileTemplate() === osmTileUrl
    }

    function setCenter(latitude, longitude) {
        centerLatitude = boundedLatitude(latitude)
        centerLongitude = normalizedLongitude(longitude)
        updateMap()
    }

    function setZoomAroundScreenPoint(nextZoom, screenX, screenY, latitude, longitude) {
        zoom = boundedZoom(nextZoom)
        var point = latLonToPixel(latitude, longitude, zoom)
        var nextCenter = pixelToLatLon(point.x - screenX + mapArea.width / 2,
                                       point.y - screenY + mapArea.height / 2,
                                       zoom)
        centerLatitude = nextCenter.latitude
        centerLongitude = nextCenter.longitude
        updateMap()
    }

    function setZoomAroundCenter(nextZoom) {
        setZoomAroundScreenPoint(nextZoom, mapArea.width / 2, mapArea.height / 2,
                                 centerLatitude, centerLongitude)
    }

    function animateZoomAroundCenter(nextZoom) {
        zoomAnimation.stop()
        zoomAnimation.from = zoom
        zoomAnimation.to = boundedZoom(nextZoom)
        zoomAnimation.restart()
    }

    function initializeView() {
        if (initialized) {
            return
        }

        if (stumblefish.status.hasFix) {
            initialized = true
            zoom = defaultReportZoom
            setCenter(Number(stumblefish.status.latitude), Number(stumblefish.status.longitude))
        } else if (mapSummaryValue("hasLatest", false)) {
            initialized = true
            zoom = defaultReportZoom
            setCenter(Number(mapSummaryValue("latestLatitude", 0)),
                      Number(mapSummaryValue("latestLongitude", 0)))
        } else if (reportCount() === 0) {
            initialized = true
            zoom = 2
            setCenter(0, 0)
        }
    }

    function centerOnCurrentFix() {
        if (stumblefish.status.hasFix) {
            zoom = defaultReportZoom
            setCenter(Number(stumblefish.status.latitude), Number(stumblefish.status.longitude))
        }
    }

    function tileUrl(tile) {
        var template = tileTemplate()
        if (template.length === 0) {
            return ""
        }
        return template.replace("{z}", tile.z)
                       .replace("{x}", tile.xWrapped)
                       .replace("{y}", tile.y)
    }

    function tileScale(tile) {
        return Math.pow(2, zoom - tile.z)
    }

    function tileLeft(tile) {
        return tile.x * tileSize * tileScale(tile) - topLeftPixel.x
    }

    function tileTop(tile) {
        return tile.y * tileSize * tileScale(tile) - topLeftPixel.y
    }

    function tileDisplaySize(tile) {
        return tileSize * tileScale(tile)
    }

    function updateTopLeft() {
        var center = latLonToPixel(centerLatitude, centerLongitude, zoom)
        topLeftPixel = {
            "x": center.x - mapArea.width / 2,
            "y": center.y - mapArea.height / 2
        }
    }

    function createTileModel(z) {
        var tiles = []
        if (tileTemplate().length === 0 || mapArea.width <= 0 || mapArea.height <= 0) {
            return tiles
        }

        var tileCount = Math.pow(2, z)
        var zoomFactor = Math.pow(2, z - zoom)
        var startX = Math.floor(topLeftPixel.x * zoomFactor / tileSize) - 1
        var endX = Math.floor((topLeftPixel.x + mapArea.width) * zoomFactor / tileSize) + 1
        var startY = Math.max(0, Math.floor(topLeftPixel.y * zoomFactor / tileSize) - 1)
        var endY = Math.min(tileCount - 1,
                            Math.floor((topLeftPixel.y + mapArea.height) * zoomFactor / tileSize) + 1)

        for (var y = startY; y <= endY; ++y) {
            for (var x = startX; x <= endX; ++x) {
                var wrappedX = ((x % tileCount) + tileCount) % tileCount
                tiles.push({
                    "x": x,
                    "xWrapped": wrappedX,
                    "y": y,
                    "z": z
                })
            }
        }
        return tiles
    }

    function tilesKey(tiles) {
        if (tiles.length === 0) {
            return ""
        }

        var first = tiles[0]
        var last = tiles[tiles.length - 1]
        return first.z + ":" + tiles.length + ":" + first.x + ":" + first.y
                + ":" + last.x + ":" + last.y
    }

    function noteCurrentTileReady() {
        currentTileReadyCount += 1
        if (showPreviousTiles
                && currentTileReadyCount >= Math.max(1, Math.ceil(currentTileTargetCount * 0.65))) {
            releasePreviousTiles()
        }
    }

    function releasePreviousTiles() {
        showPreviousTiles = false
        tileLoadFallbackTimer.stop()
        previousTileClearTimer.restart()
    }

    function updateTiles() {
        if (tileTemplate().length === 0) {
            tileModel = []
            previousTileModel = []
            currentTileKey = ""
            previousTileKey = ""
            showPreviousTiles = false
            tileLoadFallbackTimer.stop()
            previousTileClearTimer.stop()
            return
        }

        var nextTileZoom = tileRequestZoom()
        var switchingZoom = nextTileZoom !== tileZoom
        if (switchingZoom && tileModel.length > 0) {
            previousTileZoom = tileZoom
            previousTileModel = tileModel
            previousTileKey = currentTileKey
            showPreviousTiles = true
            previousTileClearTimer.stop()
            tileLoadFallbackTimer.restart()
        } else if (showPreviousTiles) {
            var nextPreviousTiles = createTileModel(previousTileZoom)
            var nextPreviousKey = tilesKey(nextPreviousTiles)
            if (nextPreviousKey !== previousTileKey) {
                previousTileModel = nextPreviousTiles
                previousTileKey = nextPreviousKey
            }
        }

        tileZoom = nextTileZoom
        var nextTiles = createTileModel(tileZoom)
        var nextTileKey = tilesKey(nextTiles)
        if (switchingZoom || nextTileKey !== currentTileKey) {
            currentTileReadyCount = 0
            currentTileTargetCount = nextTiles.length
            tileModel = nextTiles
            currentTileKey = nextTileKey
        } else {
            currentTileTargetCount = tileModel.length
        }

        if (currentTileTargetCount === 0) {
            tileLoadFallbackTimer.stop()
            showPreviousTiles = false
        }
    }

    function updateMap() {
        updateTopLeft()
        updateTiles()
        heatCanvas.requestPaint()
        cellRequestTimer.restart()
    }

    function requestVisibleCells() {
        if (mapArea.width <= 0 || mapArea.height <= 0) {
            return
        }

        var margin = Math.max(mapArea.width, mapArea.height) * 0.1
        var topLeft = pixelToLatLon(topLeftPixel.x - margin, topLeftPixel.y - margin, zoom)
        var bottomRight = pixelToLatLon(topLeftPixel.x + mapArea.width + margin,
                                        topLeftPixel.y + mapArea.height + margin,
                                        zoom)
        var minLat = Math.min(topLeft.latitude, bottomRight.latitude)
        var maxLat = Math.max(topLeft.latitude, bottomRight.latitude)
        var span = mapArea.width + margin * 2
        var leftLon = span >= worldSize(zoom) ? -180 : topLeft.longitude
        var rightLon = span >= worldSize(zoom) ? 180 : bottomRight.longitude
        stumblefish.requestMapCells(minLat, leftLon, maxLat, rightLon, tileRequestZoom())
    }

    function screenPoint(latitude, longitude) {
        var point = latLonToPixel(latitude, longitude, zoom)
        var size = worldSize(zoom)
        var x = point.x
        while (x - topLeftPixel.x < -size / 2) {
            x += size
        }
        while (x - topLeftPixel.x > size / 2) {
            x -= size
        }
        return { "x": x - topLeftPixel.x, "y": point.y - topLeftPixel.y }
    }

    Component.onCompleted: {
        stumblefish.refresh()
        stumblefish.refreshMapSummary()
        initializeTimer.start()
        updateMap()
    }

    Connections {
        target: stumblefish
        onStatusChanged: initializeView()
        onSettingsChanged: updateMap()
        onMapSummaryChanged: initializeView()
        onMapCellsChanged: heatCanvas.requestPaint()
        onReportsChanged: {
            stumblefish.refreshMapSummary()
            cellRequestTimer.restart()
        }
    }

    Timer {
        id: initializeTimer
        interval: 200
        onTriggered: initializeView()
    }

    Timer {
        id: cellRequestTimer
        interval: 250
        onTriggered: requestVisibleCells()
    }

    Timer {
        id: tileLoadFallbackTimer
        interval: 900
        onTriggered: releasePreviousTiles()
    }

    Timer {
        id: previousTileClearTimer
        interval: 220
        onTriggered: {
            if (!showPreviousTiles) {
                previousTileModel = []
            }
        }
    }

    NumberAnimation {
        id: zoomAnimation
        target: page
        property: "zoom"
        duration: 180
        easing.type: Easing.InOutQuad
    }

    PageHeader {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        title: "Report map"
    }

    Item {
        id: mapArea
        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true

        onWidthChanged: updateMap()
        onHeightChanged: updateMap()

        Rectangle {
            anchors.fill: parent
            color: Theme.rgba(Theme.highlightBackgroundColor, 0.08)
        }

        Item {
            id: previousTileLayer
            anchors.fill: parent
            visible: opacity > 0
            opacity: showPreviousTiles ? 1.0 : 0.0
            z: 0

            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }

            Repeater {
                model: previousTileModel
                delegate: Image {
                    x: tileLeft(modelData)
                    y: tileTop(modelData)
                    width: tileDisplaySize(modelData)
                    height: width
                    source: tileUrl(modelData)
                    asynchronous: true
                    cache: true
                    smooth: true
                    fillMode: Image.PreserveAspectCrop
                }
            }
        }

        Item {
            id: tileLayer
            anchors.fill: parent
            z: 1

            Repeater {
                model: tileModel
                delegate: Image {
                    x: tileLeft(modelData)
                    y: tileTop(modelData)
                    width: tileDisplaySize(modelData)
                    height: width
                    source: tileUrl(modelData)
                    asynchronous: true
                    cache: true
                    smooth: true
                    fillMode: Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    property bool countedReady: false

                    function markReady() {
                        if (!countedReady && (status === Image.Ready || status === Image.Error)) {
                            countedReady = true
                            noteCurrentTileReady()
                        }
                    }

                    Behavior on opacity {
                        NumberAnimation { duration: 120 }
                    }

                    onStatusChanged: markReady()
                    Component.onCompleted: markReady()
                }
            }
        }

        Canvas {
            id: heatCanvas
            anchors.fill: parent
            z: 2
            renderTarget: Canvas.Image

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                var cells = stumblefish.mapCells || []
                for (var i = 0; i < cells.length; ++i) {
                    var cell = cells[i]
                    var polygon = cell.polygon || []
                    if (polygon.length < 3) {
                        continue
                    }

                    ctx.beginPath()
                    for (var p = 0; p < polygon.length; ++p) {
                        var point = screenPoint(Number(polygon[p].latitude), Number(polygon[p].longitude))
                        if (p === 0) {
                            ctx.moveTo(point.x, point.y)
                        } else {
                            ctx.lineTo(point.x, point.y)
                        }
                    }
                    ctx.closePath()

                    var heat = Math.max(0, Math.min(1, Number(cell.heat)))
                    var alpha = Math.min(0.78, 0.22 + heat * 0.48)
                    ctx.fillStyle = "rgba(115, 60, 210, " + alpha + ")"
                    ctx.strokeStyle = "rgba(235, 225, 255, 0.65)"
                    ctx.lineWidth = 1
                    ctx.fill()
                    ctx.stroke()
                }

                if (stumblefish.status.hasFix) {
                    var fix = screenPoint(Number(stumblefish.status.latitude),
                                          Number(stumblefish.status.longitude))
                    ctx.beginPath()
                    ctx.arc(fix.x, fix.y, 6, 0, Math.PI * 2)
                    ctx.fillStyle = "rgba(30, 160, 255, 0.95)"
                    ctx.strokeStyle = "rgba(255, 255, 255, 0.9)"
                    ctx.lineWidth = 2
                    ctx.fill()
                    ctx.stroke()
                }
            }
        }

        PinchArea {
            id: gestureArea
            anchors.fill: parent
            z: 3
            property bool pinching: false
            property real startZoom: zoom
            property real anchorLatitude: 0
            property real anchorLongitude: 0

            onPinchStarted: {
                zoomAnimation.stop()
                pinching = true
                mapInteractionActive = true
                startZoom = zoom
                var anchor = pixelToLatLon(topLeftPixel.x + pinch.center.x,
                                           topLeftPixel.y + pinch.center.y,
                                           zoom)
                anchorLatitude = anchor.latitude
                anchorLongitude = anchor.longitude
            }

            onPinchUpdated: {
                var scale = Math.max(0.25, Math.min(4, Number(pinch.scale)))
                var zoomOffset = Math.log(scale) / Math.log(2)
                setZoomAroundScreenPoint(startZoom + zoomOffset,
                                         pinch.center.x, pinch.center.y,
                                         anchorLatitude, anchorLongitude)
            }

            onPinchFinished: {
                pinching = false
                mapInteractionActive = false
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                enabled: !gestureArea.pinching
                preventStealing: true
                property real lastX: 0
                property real lastY: 0

                onPressed: {
                    mapInteractionActive = true
                    lastX = mouse.x
                    lastY = mouse.y
                }
                onReleased: mapInteractionActive = false
                onCanceled: mapInteractionActive = false
                onPositionChanged: {
                    if (!dragArea.pressed) {
                        return
                    }
                    var dx = mouse.x - lastX
                    var dy = mouse.y - lastY
                    lastX = mouse.x
                    lastY = mouse.y

                    var center = latLonToPixel(centerLatitude, centerLongitude, zoom)
                    var next = pixelToLatLon(center.x - dx, center.y - dy, zoom)
                    centerLatitude = next.latitude
                    centerLongitude = next.longitude
                    updateMap()
                }
            }
        }

        Column {
            id: mapControls
            anchors {
                top: parent.top
                right: parent.right
                margins: Theme.paddingMedium
            }
            spacing: Theme.paddingSmall
            z: 4

            Rectangle {
                width: Theme.itemSizeMedium
                height: Theme.itemSizeMedium
                radius: 4
                color: Theme.rgba(Theme.highlightBackgroundColor, 0.88)

                IconButton {
                    anchors.fill: parent
                    enabled: stumblefish.status.hasFix
                    icon.source: "image://theme/icon-m-whereami"
                    onClicked: centerOnCurrentFix()
                }
            }

            Rectangle {
                width: Theme.itemSizeMedium
                height: Theme.itemSizeMedium
                radius: 4
                color: Theme.rgba(Theme.highlightBackgroundColor, 0.88)

                IconButton {
                    anchors.fill: parent
                    icon.source: "image://theme/icon-m-add"
                    onClicked: animateZoomAroundCenter(zoom + 1)
                }
            }

            Rectangle {
                width: Theme.itemSizeMedium
                height: Theme.itemSizeMedium
                radius: 4
                color: Theme.rgba(Theme.highlightBackgroundColor, 0.88)

                IconButton {
                    anchors.fill: parent
                    icon.source: "image://theme/icon-m-remove"
                    onClicked: animateZoomAroundCenter(zoom - 1)
                }
            }
        }

        ViewPlaceholder {
            enabled: reportCount() === 0
            text: "No reports"
            z: 4
        }

        Rectangle {
            visible: isOsmTiles()
            z: 4
            anchors {
                right: parent.right
                bottom: parent.bottom
                margins: Theme.paddingSmall
            }
            color: Theme.rgba(Theme.highlightBackgroundColor, 0.65)
            width: attribution.width + 2 * Theme.paddingSmall
            height: attribution.height + 2 * Theme.paddingSmall

            Label {
                id: attribution
                anchors.centerIn: parent
                text: "© OpenStreetMap contributors"
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }
}
