// SPDX-License-Identifier: MIT
#include "storage.h"

#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtMath>
#include <QVariantList>

namespace {

const double Pi = 3.14159265358979323846;
const double MaxMercatorLatitude = 85.05112878;
const double HexRadiusPixels = 22.0;
const int MapAggregationZooms[] = { 4, 7, 10, 12, 14, 16, 18 };
const int MapAggregationZoomCount = sizeof(MapAggregationZooms) / sizeof(MapAggregationZooms[0]);

struct HexCoord
{
    int q;
    int r;
};

struct MapCellKey
{
    int zoom;
    int q;
    int r;
};

struct StatusCounts
{
    StatusCounts()
        : pending(0)
        , failed(0)
        , uploaded(0)
        , uploading(0)
    {
    }

    int pending;
    int failed;
    int uploaded;
    int uploading;
};

QString databasePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/reports.sqlite");
}

QVariantList wifiToList(const QList<WifiObservation> &observations)
{
    QVariantList list;
    foreach (const WifiObservation &wifi, observations) {
        QVariantMap map;
        map.insert(QStringLiteral("macAddress"), wifi.macAddress);
        map.insert(QStringLiteral("frequency"), wifi.frequency);
        map.insert(QStringLiteral("signalStrength"), wifi.signalStrength);
        map.insert(QStringLiteral("seenMs"), wifi.seenMs);
        list.append(map);
    }
    return list;
}

QVariantList cellsToList(const QList<CellObservation> &observations)
{
    QVariantList list;
    foreach (const CellObservation &cell, observations) {
        QVariantMap map;
        map.insert(QStringLiteral("radioType"), cell.radioType);
        map.insert(QStringLiteral("mobileCountryCode"), cell.mobileCountryCode);
        map.insert(QStringLiteral("mobileNetworkCode"), cell.mobileNetworkCode);
        map.insert(QStringLiteral("locationAreaCode"), cell.locationAreaCode);
        map.insert(QStringLiteral("cellId"), cell.cellId);
        map.insert(QStringLiteral("primaryScramblingCode"), cell.primaryScramblingCode);
        map.insert(QStringLiteral("signalStrength"), cell.signalStrength);
        map.insert(QStringLiteral("serving"), cell.serving);
        map.insert(QStringLiteral("seenMs"), cell.seenMs);
        list.append(map);
    }
    return list;
}

QVariantList bleToList(const QList<BleObservation> &observations)
{
    QVariantList list;
    foreach (const BleObservation &ble, observations) {
        QVariantMap map;
        map.insert(QStringLiteral("macAddress"), ble.macAddress);
        map.insert(QStringLiteral("addressType"), ble.addressType);
        map.insert(QStringLiteral("name"), ble.name);
        map.insert(QStringLiteral("signalStrength"), ble.signalStrength);
        map.insert(QStringLiteral("beaconType"), ble.beaconType);
        map.insert(QStringLiteral("id1"), ble.id1);
        map.insert(QStringLiteral("id2"), ble.id2);
        map.insert(QStringLiteral("id3"), ble.id3);
        map.insert(QStringLiteral("uuids"), ble.uuids);
        map.insert(QStringLiteral("manufacturerData"), ble.manufacturerData);
        map.insert(QStringLiteral("serviceData"), ble.serviceData);
        map.insert(QStringLiteral("seenMs"), ble.seenMs);
        list.append(map);
    }
    return list;
}

QString stringListToJson(const QStringList &list)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(list).toJson(QJsonDocument::Compact));
}

QStringList jsonToStringList(const QString &json)
{
    QStringList result;
    const QVariant value = QJsonDocument::fromJson(json.toUtf8()).toVariant();
    foreach (const QVariant &item, value.toList()) {
        result.append(item.toString());
    }
    return result;
}

double boundedLatitude(double latitude)
{
    return qBound(-MaxMercatorLatitude, latitude, MaxMercatorLatitude);
}

double normalizedLongitude(double longitude)
{
    double result = longitude;
    while (result < -180.0) {
        result += 360.0;
    }
    while (result > 180.0) {
        result -= 360.0;
    }
    return result;
}

double worldSizeForZoom(int zoom)
{
    return 256.0 * qPow(2.0, qBound(0, zoom, 20));
}

int aggregationZoomForMapZoom(int zoom)
{
    const int bounded = qBound(1, zoom, 19);
    int best = MapAggregationZooms[0];
    int bestDistance = qAbs(bounded - best);
    for (int i = 1; i < MapAggregationZoomCount; ++i) {
        const int distance = qAbs(bounded - MapAggregationZooms[i]);
        if (distance < bestDistance) {
            best = MapAggregationZooms[i];
            bestDistance = distance;
        }
    }
    return best;
}

QPointF latLonToPixel(double latitude, double longitude, int zoom)
{
    const double worldSize = worldSizeForZoom(zoom);
    const double lat = boundedLatitude(latitude) * Pi / 180.0;
    const double lon = normalizedLongitude(longitude);
    const double x = (lon + 180.0) / 360.0 * worldSize;
    const double y = (0.5 - qLn((1.0 + qSin(lat)) / (1.0 - qSin(lat))) / (4.0 * Pi)) * worldSize;
    return QPointF(x, y);
}

QPointF pixelToLatLon(double x, double y, int zoom)
{
    const double worldSize = worldSizeForZoom(zoom);
    const double lon = normalizedLongitude(x / worldSize * 360.0 - 180.0);
    const double n = Pi - 2.0 * Pi * y / worldSize;
    const double lat = 180.0 / Pi * qAtan(0.5 * (qExp(n) - qExp(-n)));
    return QPointF(boundedLatitude(lat), lon);
}

HexCoord roundedHexCoord(double q, double r)
{
    const double x = q;
    const double z = r;
    const double y = -x - z;

    int rx = qRound(x);
    int ry = qRound(y);
    int rz = qRound(z);

    const double xDiff = qAbs(rx - x);
    const double yDiff = qAbs(ry - y);
    const double zDiff = qAbs(rz - z);

    if (xDiff > yDiff && xDiff > zDiff) {
        rx = -ry - rz;
    } else if (yDiff > zDiff) {
        ry = -rx - rz;
    } else {
        rz = -rx - ry;
    }

    HexCoord coord;
    coord.q = rx;
    coord.r = rz;
    return coord;
}

HexCoord pixelToHex(const QPointF &point)
{
    return roundedHexCoord((qSqrt(3.0) / 3.0 * point.x() - point.y() / 3.0) / HexRadiusPixels,
                           (2.0 / 3.0 * point.y()) / HexRadiusPixels);
}

QPointF hexToPixel(const HexCoord &coord)
{
    return QPointF(HexRadiusPixels * qSqrt(3.0) * (coord.q + coord.r / 2.0),
                   HexRadiusPixels * 3.0 / 2.0 * coord.r);
}

QString keyString(const MapCellKey &key)
{
    return QString::number(key.zoom) + QStringLiteral(":")
            + QString::number(key.q) + QStringLiteral(":")
            + QString::number(key.r);
}

StatusCounts countsForStatus(const QString &status, int amount = 1)
{
    StatusCounts counts;
    if (status == QStringLiteral("pending")) {
        counts.pending = amount;
    } else if (status == QStringLiteral("failed")) {
        counts.failed = amount;
    } else if (status == QStringLiteral("uploaded")) {
        counts.uploaded = amount;
    } else if (status == QStringLiteral("uploading")) {
        counts.uploading = amount;
    }
    return counts;
}

MapCellKey mapCellKeyForReport(const Report &report, int zoom)
{
    const HexCoord coord = pixelToHex(latLonToPixel(report.position.latitude,
                                                    report.position.longitude,
                                                    zoom));
    MapCellKey key;
    key.zoom = zoom;
    key.q = coord.q;
    key.r = coord.r;
    return key;
}

QPointF centerForMapCell(const MapCellKey &key)
{
    HexCoord coord;
    coord.q = key.q;
    coord.r = key.r;
    return pixelToLatLon(hexToPixel(coord).x(), hexToPixel(coord).y(), key.zoom);
}

QVariantMap latLonMap(double latitude, double longitude)
{
    QVariantMap point;
    point.insert(QStringLiteral("latitude"), latitude);
    point.insert(QStringLiteral("longitude"), longitude);
    return point;
}

QVariantList polygonForHex(const HexCoord &coord, int zoom)
{
    QVariantList polygon;
    const QPointF center = hexToPixel(coord);
    for (int i = 0; i < 6; ++i) {
        const double angle = Pi / 180.0 * (30.0 + 60.0 * i);
        const QPointF latLon = pixelToLatLon(center.x() + HexRadiusPixels * qCos(angle),
                                             center.y() + HexRadiusPixels * qSin(angle),
                                             zoom);
        polygon.append(latLonMap(latLon.x(), latLon.y()));
    }
    return polygon;
}

QVariantMap mapCellToVariantMap(const MapCellKey &key, double centerLatitude,
                                double centerLongitude, int count, int pendingCount,
                                int failedCount, int uploadedCount, int uploadingCount,
                                qint64 latestTimestampMs)
{
    HexCoord coord;
    coord.q = key.q;
    coord.r = key.r;

    QVariantMap map;
    map.insert(QStringLiteral("id"), keyString(key));
    map.insert(QStringLiteral("latitude"), centerLatitude);
    map.insert(QStringLiteral("longitude"), centerLongitude);
    map.insert(QStringLiteral("count"), count);
    map.insert(QStringLiteral("pendingCount"), pendingCount);
    map.insert(QStringLiteral("failedCount"), failedCount);
    map.insert(QStringLiteral("uploadedCount"), uploadedCount);
    map.insert(QStringLiteral("uploadingCount"), uploadingCount);
    map.insert(QStringLiteral("latestTimestampMs"), latestTimestampMs);
    map.insert(QStringLiteral("heat"), qMin(1.0, 0.2 + count / 12.0));
    map.insert(QStringLiteral("polygon"), polygonForHex(coord, key.zoom));
    return map;
}

bool setQueryError(QSqlQuery *query, QString *error)
{
    if (error) {
        *error = query->lastError().text();
    }
    return false;
}

bool setDatabaseError(const QSqlDatabase &db, QString *error)
{
    if (error) {
        *error = db.lastError().text();
    }
    return false;
}

bool execChecked(QSqlDatabase db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        return setQueryError(&query, error);
    }
    return true;
}

bool scalarCount(QSqlDatabase db, const QString &sql, qint64 *count, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        return setQueryError(&query, error);
    }
    if (!query.next()) {
        *count = 0;
        return true;
    }
    *count = query.value(0).toLongLong();
    return true;
}

QList<MapCellKey> mapCellsForReport(QSqlDatabase db, int reportId, bool *ok, QString *error)
{
    if (ok) {
        *ok = false;
    }

    QList<MapCellKey> keys;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("select zoom, q, r from map_report_cells where report_id = ?"));
    query.addBindValue(reportId);
    if (!query.exec()) {
        setQueryError(&query, error);
        return keys;
    }

    while (query.next()) {
        MapCellKey key;
        key.zoom = query.value(0).toInt();
        key.q = query.value(1).toInt();
        key.r = query.value(2).toInt();
        keys.append(key);
    }
    if (ok) {
        *ok = true;
    }
    return keys;
}

bool insertMapCellMembership(QSqlDatabase db, int reportId, const MapCellKey &key, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("insert into map_report_cells (report_id, zoom, q, r) "
                                 "values (?, ?, ?, ?)"));
    query.addBindValue(reportId);
    query.addBindValue(key.zoom);
    query.addBindValue(key.q);
    query.addBindValue(key.r);
    if (!query.exec()) {
        return setQueryError(&query, error);
    }
    return true;
}

bool addReportToMapCell(QSqlDatabase db, const Report &report, const MapCellKey &key, QString *error)
{
    const QPointF center = centerForMapCell(key);
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("insert or ignore into map_cells "
                                  "(zoom, q, r, center_latitude, center_longitude, count, "
                                  "pending_count, failed_count, uploaded_count, uploading_count, latest_timestamp_ms) "
                                  "values (?, ?, ?, ?, ?, 0, 0, 0, 0, 0, 0)"));
    insert.addBindValue(key.zoom);
    insert.addBindValue(key.q);
    insert.addBindValue(key.r);
    insert.addBindValue(center.x());
    insert.addBindValue(center.y());
    if (!insert.exec()) {
        return setQueryError(&insert, error);
    }

    const StatusCounts counts = countsForStatus(report.uploadStatus.isEmpty()
                                                ? QStringLiteral("pending")
                                                : report.uploadStatus);
    QSqlQuery update(db);
    update.prepare(QStringLiteral("update map_cells set "
                                  "count = count + 1, "
                                  "pending_count = pending_count + ?, "
                                  "failed_count = failed_count + ?, "
                                  "uploaded_count = uploaded_count + ?, "
                                  "uploading_count = uploading_count + ?, "
                                  "latest_timestamp_ms = max(latest_timestamp_ms, ?) "
                                  "where zoom = ? and q = ? and r = ?"));
    update.addBindValue(counts.pending);
    update.addBindValue(counts.failed);
    update.addBindValue(counts.uploaded);
    update.addBindValue(counts.uploading);
    update.addBindValue(report.timestampMs);
    update.addBindValue(key.zoom);
    update.addBindValue(key.q);
    update.addBindValue(key.r);
    if (!update.exec()) {
        return setQueryError(&update, error);
    }
    return true;
}

bool addReportToMapCells(QSqlDatabase db, const Report &report, QString *error)
{
    for (int i = 0; i < MapAggregationZoomCount; ++i) {
        const MapCellKey key = mapCellKeyForReport(report, MapAggregationZooms[i]);
        if (!insertMapCellMembership(db, report.id, key, error)) {
            return false;
        }
        if (!addReportToMapCell(db, report, key, error)) {
            return false;
        }
    }
    return true;
}

bool recomputeMapCell(QSqlDatabase db, const MapCellKey &key, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("select r.upload_status, r.timestamp_ms "
                                 "from map_report_cells c "
                                 "join reports r on r.id = c.report_id "
                                 "where c.zoom = ? and c.q = ? and c.r = ?"));
    query.addBindValue(key.zoom);
    query.addBindValue(key.q);
    query.addBindValue(key.r);
    if (!query.exec()) {
        return setQueryError(&query, error);
    }

    int count = 0;
    StatusCounts counts;
    qint64 latest = 0;
    while (query.next()) {
        ++count;
        const StatusCounts statusCounts = countsForStatus(query.value(0).toString());
        counts.pending += statusCounts.pending;
        counts.failed += statusCounts.failed;
        counts.uploaded += statusCounts.uploaded;
        counts.uploading += statusCounts.uploading;
        latest = qMax(latest, query.value(1).toLongLong());
    }

    if (count == 0) {
        QSqlQuery remove(db);
        remove.prepare(QStringLiteral("delete from map_cells where zoom = ? and q = ? and r = ?"));
        remove.addBindValue(key.zoom);
        remove.addBindValue(key.q);
        remove.addBindValue(key.r);
        if (!remove.exec()) {
            return setQueryError(&remove, error);
        }
        return true;
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral("update map_cells set count = ?, pending_count = ?, "
                                  "failed_count = ?, uploaded_count = ?, uploading_count = ?, "
                                  "latest_timestamp_ms = ? where zoom = ? and q = ? and r = ?"));
    update.addBindValue(count);
    update.addBindValue(counts.pending);
    update.addBindValue(counts.failed);
    update.addBindValue(counts.uploaded);
    update.addBindValue(counts.uploading);
    update.addBindValue(latest);
    update.addBindValue(key.zoom);
    update.addBindValue(key.q);
    update.addBindValue(key.r);
    if (!update.exec()) {
        return setQueryError(&update, error);
    }
    return true;
}

bool updateMapCellsForStatus(QSqlDatabase db, int reportId, const QString &oldStatus,
                             const QString &newStatus, QString *error)
{
    if (oldStatus == newStatus) {
        return true;
    }

    const StatusCounts remove = countsForStatus(oldStatus, -1);
    const StatusCounts add = countsForStatus(newStatus, 1);
    bool ok = false;
    const QList<MapCellKey> keys = mapCellsForReport(db, reportId, &ok, error);
    if (!ok) {
        return false;
    }
    foreach (const MapCellKey &key, keys) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("update map_cells set "
                                      "pending_count = pending_count + ?, "
                                      "failed_count = failed_count + ?, "
                                      "uploaded_count = uploaded_count + ?, "
                                      "uploading_count = uploading_count + ? "
                                      "where zoom = ? and q = ? and r = ?"));
        update.addBindValue(remove.pending + add.pending);
        update.addBindValue(remove.failed + add.failed);
        update.addBindValue(remove.uploaded + add.uploaded);
        update.addBindValue(remove.uploading + add.uploading);
        update.addBindValue(key.zoom);
        update.addBindValue(key.q);
        update.addBindValue(key.r);
        if (!update.exec()) {
            return setQueryError(&update, error);
        }
    }
    return true;
}

bool rebuildMapCells(QSqlDatabase db, QString *error)
{
    if (!db.transaction()) {
        return setDatabaseError(db, error);
    }

    if (!execChecked(db, QStringLiteral("delete from map_report_cells"), error)
            || !execChecked(db, QStringLiteral("delete from map_cells"), error)) {
        db.rollback();
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("select id, timestamp_ms, latitude, longitude, upload_status "
                                   "from reports order by id"))) {
        setQueryError(&query, error);
        db.rollback();
        return false;
    }

    while (query.next()) {
        Report report;
        report.id = query.value(0).toInt();
        report.timestampMs = query.value(1).toLongLong();
        report.position.valid = true;
        report.position.timestampMs = report.timestampMs;
        report.position.latitude = query.value(2).toDouble();
        report.position.longitude = query.value(3).toDouble();
        report.uploadStatus = query.value(4).toString();
        if (!addReportToMapCells(db, report, error)) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        return setDatabaseError(db, error);
    }
    return true;
}

bool ensureMapCellsCurrent(QSqlDatabase db, QString *error)
{
    qint64 reportCount = 0;
    qint64 membershipCount = 0;
    if (!scalarCount(db, QStringLiteral("select count(*) from reports"), &reportCount, error)
            || !scalarCount(db, QStringLiteral("select count(*) from map_report_cells"),
                            &membershipCount, error)) {
        return false;
    }

    if (membershipCount != reportCount * MapAggregationZoomCount) {
        return rebuildMapCells(db, error);
    }
    return true;
}

bool recoverInterruptedUploads(QSqlDatabase db, bool *changed, QString *error)
{
    QSqlQuery count(db);
    if (!count.exec(QStringLiteral("select count(*) from reports where upload_status = 'uploading'"))) {
        return setQueryError(&count, error);
    }
    const bool hasRows = count.next() && count.value(0).toInt() > 0;
    if (changed) {
        *changed = hasRows;
    }
    if (!hasRows) {
        return true;
    }

    QSqlQuery update(db);
    if (!update.exec(QStringLiteral("update reports set upload_status = 'failed', "
                                    "retry_count = retry_count + 1, "
                                    "last_error = 'Upload interrupted before completion' "
                                    "where upload_status = 'uploading'"))) {
        return setQueryError(&update, error);
    }
    return true;
}

}

Storage::Storage(QObject *parent)
    : QObject(parent)
{
}

Storage::~Storage()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool Storage::open()
{
    if (m_db.isOpen()) {
        return true;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("stumblefish"));
    m_db.setDatabaseName(databasePath());
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return migrate();
}

QString Storage::lastError() const
{
    return m_lastError;
}

bool Storage::migrate()
{
    return exec(QStringLiteral("create table if not exists reports ("
                               "id integer primary key autoincrement,"
                               "timestamp_ms integer not null,"
                               "latitude real not null,"
                               "longitude real not null,"
                               "altitude real,"
                               "accuracy real not null,"
                               "mode text not null,"
                               "wifi_enabled integer not null,"
                               "cell_enabled integer not null,"
                               "ble_enabled integer not null,"
                               "upload_status text not null default 'pending',"
                               "retry_count integer not null default 0,"
                               "last_error text,"
                               "endpoint text,"
                               "uploaded_at_ms integer default 0"
                               ")"))
        && exec(QStringLiteral("create table if not exists wifi_observations ("
                               "id integer primary key autoincrement,"
                               "report_id integer not null,"
                               "mac_address text not null,"
                               "ssid text,"
                               "frequency integer,"
                               "signal_strength integer,"
                               "seen_ms integer not null"
                               ")"))
        && exec(QStringLiteral("create table if not exists cell_observations ("
                               "id integer primary key autoincrement,"
                               "report_id integer not null,"
                               "radio_type text not null,"
                               "mcc integer,"
                               "mnc integer,"
                               "lac integer,"
                               "cell_id integer,"
                               "psc integer,"
                               "signal_strength integer,"
                               "serving integer,"
                               "seen_ms integer not null"
                               ")"))
        && exec(QStringLiteral("create table if not exists ble_observations ("
                               "id integer primary key autoincrement,"
                               "report_id integer not null,"
                               "mac_address text not null,"
                               "address_type text,"
                               "name text,"
                               "signal_strength integer,"
                               "beacon_type integer,"
                               "id1 text,"
                               "id2 text,"
                               "id3 text,"
                               "uuids text,"
                               "manufacturer_data text,"
                               "service_data text,"
                               "seen_ms integer not null"
                               ")"))
        && exec(QStringLiteral("create table if not exists map_report_cells ("
                               "report_id integer not null,"
                               "zoom integer not null,"
                               "q integer not null,"
                               "r integer not null,"
                               "primary key (report_id, zoom)"
                               ")"))
        && exec(QStringLiteral("create table if not exists map_cells ("
                               "zoom integer not null,"
                               "q integer not null,"
                               "r integer not null,"
                               "center_latitude real not null,"
                               "center_longitude real not null,"
                               "count integer not null default 0,"
                               "pending_count integer not null default 0,"
                               "failed_count integer not null default 0,"
                               "uploaded_count integer not null default 0,"
                               "uploading_count integer not null default 0,"
                               "latest_timestamp_ms integer not null default 0,"
                               "primary key (zoom, q, r)"
                               ")"))
        && exec(QStringLiteral("create index if not exists reports_latitude_longitude_idx "
                               "on reports (latitude, longitude)"))
        && exec(QStringLiteral("create index if not exists reports_timestamp_idx "
                               "on reports (timestamp_ms)"))
        && exec(QStringLiteral("create index if not exists map_report_cells_cell_idx "
                               "on map_report_cells (zoom, q, r)"))
        && exec(QStringLiteral("create index if not exists map_cells_center_idx "
                               "on map_cells (zoom, center_latitude, center_longitude)"))
        && exec(QStringLiteral("update wifi_observations set ssid = '' "
                               "where ssid is not null and ssid != ''"))
        && [this]() {
            bool uploadsRecovered = false;
            if (!recoverInterruptedUploads(m_db, &uploadsRecovered, &m_lastError)) {
                return false;
            }
            return uploadsRecovered
                    ? rebuildMapCells(m_db, &m_lastError)
                    : ensureMapCellsCurrent(m_db, &m_lastError);
        }();
}

bool Storage::exec(const QString &sql) const
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

int Storage::addReport(const Report &report)
{
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return 0;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("insert into reports "
                                 "(timestamp_ms, latitude, longitude, altitude, accuracy, mode, "
                                 "wifi_enabled, cell_enabled, ble_enabled, upload_status, retry_count, endpoint) "
                                 "values (?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', 0, ?)"));
    query.addBindValue(report.timestampMs);
    query.addBindValue(report.position.latitude);
    query.addBindValue(report.position.longitude);
    query.addBindValue(report.position.altitude);
    query.addBindValue(report.position.accuracy);
    query.addBindValue(report.mode);
    query.addBindValue(report.wifiEnabled ? 1 : 0);
    query.addBindValue(report.cellEnabled ? 1 : 0);
    query.addBindValue(report.bleEnabled ? 1 : 0);
    query.addBindValue(report.endpoint);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        m_db.rollback();
        return 0;
    }

    const int reportId = query.lastInsertId().toInt();

    foreach (const WifiObservation &wifi, report.wifi) {
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("insert into wifi_observations "
                                      "(report_id, mac_address, frequency, signal_strength, seen_ms) "
                                      "values (?, ?, ?, ?, ?)"));
        insert.addBindValue(reportId);
        insert.addBindValue(wifi.macAddress);
        insert.addBindValue(wifi.frequency);
        insert.addBindValue(wifi.signalStrength);
        insert.addBindValue(wifi.seenMs);
        if (!insert.exec()) {
            m_lastError = insert.lastError().text();
            m_db.rollback();
            return 0;
        }
    }

    foreach (const CellObservation &cell, report.cells) {
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("insert into cell_observations "
                                      "(report_id, radio_type, mcc, mnc, lac, cell_id, psc, signal_strength, serving, seen_ms) "
                                      "values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(reportId);
        insert.addBindValue(cell.radioType);
        insert.addBindValue(cell.mobileCountryCode);
        insert.addBindValue(cell.mobileNetworkCode);
        insert.addBindValue(cell.locationAreaCode);
        insert.addBindValue(cell.cellId);
        insert.addBindValue(cell.primaryScramblingCode);
        insert.addBindValue(cell.signalStrength);
        insert.addBindValue(cell.serving ? 1 : 0);
        insert.addBindValue(cell.seenMs);
        if (!insert.exec()) {
            m_lastError = insert.lastError().text();
            m_db.rollback();
            return 0;
        }
    }

    foreach (const BleObservation &ble, report.ble) {
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("insert into ble_observations "
                                      "(report_id, mac_address, address_type, name, signal_strength, beacon_type, "
                                      "id1, id2, id3, uuids, manufacturer_data, service_data, seen_ms) "
                                      "values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(reportId);
        insert.addBindValue(ble.macAddress);
        insert.addBindValue(ble.addressType);
        insert.addBindValue(ble.name);
        insert.addBindValue(ble.signalStrength);
        insert.addBindValue(ble.beaconType);
        insert.addBindValue(ble.id1);
        insert.addBindValue(ble.id2);
        insert.addBindValue(ble.id3);
        insert.addBindValue(stringListToJson(ble.uuids));
        insert.addBindValue(ble.manufacturerData);
        insert.addBindValue(ble.serviceData);
        insert.addBindValue(ble.seenMs);
        if (!insert.exec()) {
            m_lastError = insert.lastError().text();
            m_db.rollback();
            return 0;
        }
    }

    Report storedReport = report;
    storedReport.id = reportId;
    storedReport.uploadStatus = QStringLiteral("pending");
    if (!addReportToMapCells(m_db, storedReport, &m_lastError)) {
        m_db.rollback();
        return 0;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        return 0;
    }

    emit changed();
    return reportId;
}

QList<Report> Storage::recentReports(int limit) const
{
    QList<Report> reports;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select id, timestamp_ms, latitude, longitude, altitude, accuracy, mode, "
                                 "wifi_enabled, cell_enabled, ble_enabled, upload_status, retry_count, "
                                 "last_error, endpoint, uploaded_at_ms "
                                 "from reports order by id desc limit ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return reports;
    }

    while (query.next()) {
        Report report;
        report.id = query.value(0).toInt();
        report.timestampMs = query.value(1).toLongLong();
        report.position.valid = true;
        report.position.timestampMs = report.timestampMs;
        report.position.latitude = query.value(2).toDouble();
        report.position.longitude = query.value(3).toDouble();
        report.position.altitude = query.value(4).toDouble();
        report.position.accuracy = query.value(5).toDouble();
        report.mode = query.value(6).toString();
        report.wifiEnabled = query.value(7).toInt() != 0;
        report.cellEnabled = query.value(8).toInt() != 0;
        report.bleEnabled = query.value(9).toInt() != 0;
        report.uploadStatus = query.value(10).toString();
        report.retryCount = query.value(11).toInt();
        report.lastError = query.value(12).toString();
        report.endpoint = query.value(13).toString();
        report.uploadedAtMs = query.value(14).toLongLong();
        report.wifi = wifiForReport(report.id);
        report.cells = cellsForReport(report.id);
        report.ble = bleForReport(report.id);
        reports.append(report);
    }
    return reports;
}

QList<Report> Storage::unuploadedReports(int limit) const
{
    QList<Report> reports;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select id, timestamp_ms, latitude, longitude, altitude, accuracy, mode, "
                                 "wifi_enabled, cell_enabled, ble_enabled, upload_status, retry_count, "
                                 "last_error, endpoint, uploaded_at_ms "
                                 "from reports where upload_status != 'uploaded' "
                                 "order by id desc limit ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return reports;
    }

    while (query.next()) {
        Report report;
        report.id = query.value(0).toInt();
        report.timestampMs = query.value(1).toLongLong();
        report.position.valid = true;
        report.position.timestampMs = report.timestampMs;
        report.position.latitude = query.value(2).toDouble();
        report.position.longitude = query.value(3).toDouble();
        report.position.altitude = query.value(4).toDouble();
        report.position.accuracy = query.value(5).toDouble();
        report.mode = query.value(6).toString();
        report.wifiEnabled = query.value(7).toInt() != 0;
        report.cellEnabled = query.value(8).toInt() != 0;
        report.bleEnabled = query.value(9).toInt() != 0;
        report.uploadStatus = query.value(10).toString();
        report.retryCount = query.value(11).toInt();
        report.lastError = query.value(12).toString();
        report.endpoint = query.value(13).toString();
        report.uploadedAtMs = query.value(14).toLongLong();
        report.wifi = wifiForReport(report.id);
        report.cells = cellsForReport(report.id);
        report.ble = bleForReport(report.id);
        reports.append(report);
    }
    return reports;
}

Report Storage::report(int id) const
{
    QList<Report> reports;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select id, timestamp_ms, latitude, longitude, altitude, accuracy, mode, "
                                 "wifi_enabled, cell_enabled, ble_enabled, upload_status, retry_count, "
                                 "last_error, endpoint, uploaded_at_ms "
                                 "from reports where id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return Report();
    }

    if (!query.next()) {
        return Report();
    }

    Report report;
    report.id = query.value(0).toInt();
    report.timestampMs = query.value(1).toLongLong();
    report.position.valid = true;
    report.position.timestampMs = report.timestampMs;
    report.position.latitude = query.value(2).toDouble();
    report.position.longitude = query.value(3).toDouble();
    report.position.altitude = query.value(4).toDouble();
    report.position.accuracy = query.value(5).toDouble();
    report.mode = query.value(6).toString();
    report.wifiEnabled = query.value(7).toInt() != 0;
    report.cellEnabled = query.value(8).toInt() != 0;
    report.bleEnabled = query.value(9).toInt() != 0;
    report.uploadStatus = query.value(10).toString();
    report.retryCount = query.value(11).toInt();
    report.lastError = query.value(12).toString();
    report.endpoint = query.value(13).toString();
    report.uploadedAtMs = query.value(14).toLongLong();
    report.wifi = wifiForReport(report.id);
    report.cells = cellsForReport(report.id);
    report.ble = bleForReport(report.id);
    return report;
}

QList<Report> Storage::uploadCandidates(int limit, int maxRetryCount) const
{
    QList<Report> reports;
    QSqlQuery query(m_db);
    QString sql = QStringLiteral("select id from reports "
                                 "where upload_status in ('pending', 'failed') ");
    if (maxRetryCount >= 0) {
        sql += QStringLiteral("and retry_count < ? ");
    }
    sql += QStringLiteral("order by id asc limit ?");
    query.prepare(sql);
    if (maxRetryCount >= 0) {
        query.addBindValue(maxRetryCount);
    }
    query.addBindValue(limit);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return reports;
    }
    while (query.next()) {
        reports.append(report(query.value(0).toInt()));
    }
    return reports;
}

qint64 Storage::lastReportTimestamp() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("select timestamp_ms from reports order by timestamp_ms desc limit 1"))) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toLongLong();
}

QVariantMap Storage::mapSummary() const
{
    QVariantMap summary;
    QSqlQuery bounds(m_db);
    if (!bounds.exec(QStringLiteral("select count(*), min(latitude), min(longitude), "
                                    "max(latitude), max(longitude) from reports"))) {
        const_cast<Storage *>(this)->m_lastError = bounds.lastError().text();
        return summary;
    }

    int total = 0;
    if (bounds.next()) {
        total = bounds.value(0).toInt();
        summary.insert(QStringLiteral("total"), total);
        summary.insert(QStringLiteral("hasBounds"), total > 0);
        if (total > 0) {
            summary.insert(QStringLiteral("minLatitude"), bounds.value(1).toDouble());
            summary.insert(QStringLiteral("minLongitude"), bounds.value(2).toDouble());
            summary.insert(QStringLiteral("maxLatitude"), bounds.value(3).toDouble());
            summary.insert(QStringLiteral("maxLongitude"), bounds.value(4).toDouble());
        }
    }

    QSqlQuery latest(m_db);
    if (!latest.exec(QStringLiteral("select timestamp_ms, latitude, longitude from reports "
                                    "order by timestamp_ms desc limit 1"))) {
        const_cast<Storage *>(this)->m_lastError = latest.lastError().text();
        return summary;
    }

    if (latest.next()) {
        summary.insert(QStringLiteral("hasLatest"), true);
        summary.insert(QStringLiteral("latestTimestampMs"), latest.value(0).toLongLong());
        summary.insert(QStringLiteral("latestLatitude"), latest.value(1).toDouble());
        summary.insert(QStringLiteral("latestLongitude"), latest.value(2).toDouble());
    } else {
        summary.insert(QStringLiteral("hasLatest"), false);
        summary.insert(QStringLiteral("latestTimestampMs"), 0);
    }

    return summary;
}

QVariantList Storage::mapCells(double minLatitude, double minLongitude,
                               double maxLatitude, double maxLongitude, int zoom) const
{
    const double boundedMinLatitude = boundedLatitude(qMin(minLatitude, maxLatitude));
    const double boundedMaxLatitude = boundedLatitude(qMax(minLatitude, maxLatitude));
    const double boundedMinLongitude = normalizedLongitude(minLongitude);
    const double boundedMaxLongitude = normalizedLongitude(maxLongitude);
    const bool crossesAntimeridian = boundedMinLongitude > boundedMaxLongitude;
    const int aggregateZoom = aggregationZoomForMapZoom(zoom);

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select zoom, q, r, center_latitude, center_longitude, count, "
                                 "pending_count, failed_count, uploaded_count, uploading_count, "
                                 "latest_timestamp_ms from map_cells "
                                 "where zoom = ? and center_latitude >= ? and center_latitude <= ? and "
                                 "((? = 0 and center_longitude >= ? and center_longitude <= ?) "
                                 "or (? = 1 and (center_longitude >= ? or center_longitude <= ?)))"));
    query.addBindValue(aggregateZoom);
    query.addBindValue(boundedMinLatitude);
    query.addBindValue(boundedMaxLatitude);
    query.addBindValue(crossesAntimeridian ? 1 : 0);
    query.addBindValue(boundedMinLongitude);
    query.addBindValue(boundedMaxLongitude);
    query.addBindValue(crossesAntimeridian ? 1 : 0);
    query.addBindValue(boundedMinLongitude);
    query.addBindValue(boundedMaxLongitude);

    QVariantList cells;
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return QVariantList();
    }

    while (query.next()) {
        MapCellKey key;
        key.zoom = query.value(0).toInt();
        key.q = query.value(1).toInt();
        key.r = query.value(2).toInt();
        cells.append(mapCellToVariantMap(key,
                                         query.value(3).toDouble(),
                                         query.value(4).toDouble(),
                                         query.value(5).toInt(),
                                         query.value(6).toInt(),
                                         query.value(7).toInt(),
                                         query.value(8).toInt(),
                                         query.value(9).toInt(),
                                         query.value(10).toLongLong()));
    }

    return cells;
}

QVariantMap Storage::counts() const
{
    QVariantMap counts;
    const QStringList statuses = QStringList()
            << QStringLiteral("pending")
            << QStringLiteral("uploading")
            << QStringLiteral("uploaded")
            << QStringLiteral("failed");

    foreach (const QString &status, statuses) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("select count(*) from reports where upload_status = ?"));
        query.addBindValue(status);
        if (query.exec() && query.next()) {
            counts.insert(status, query.value(0).toInt());
        } else {
            counts.insert(status, 0);
        }
    }

    QSqlQuery total(m_db);
    if (total.exec(QStringLiteral("select count(*) from reports")) && total.next()) {
        counts.insert(QStringLiteral("total"), total.value(0).toInt());
    } else {
        counts.insert(QStringLiteral("total"), 0);
    }
    return counts;
}

bool Storage::deleteReport(int id)
{
    if (id <= 0) {
        m_lastError = QStringLiteral("Invalid report id");
        return false;
    }
    return deleteReports(QList<int>() << id);
}

int Storage::clearPendingReports()
{
    QList<int> ids;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("select id from reports "
                                   "where upload_status in ('pending', 'failed') "
                                   "order by id"))) {
        m_lastError = query.lastError().text();
        return -1;
    }

    while (query.next()) {
        ids.append(query.value(0).toInt());
    }

    if (!deleteReports(ids)) {
        return -1;
    }
    return ids.count();
}

int Storage::pruneReportsOlderThan(qint64 cutoffMs)
{
    QList<int> ids;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select id from reports where timestamp_ms < ? order by id"));
    query.addBindValue(cutoffMs);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }

    while (query.next()) {
        ids.append(query.value(0).toInt());
    }

    if (!deleteReports(ids)) {
        return -1;
    }
    return ids.count();
}

bool Storage::markUploading(const QList<int> &ids)
{
    return updateStatus(ids, QStringLiteral("uploading"), QString(), false);
}

bool Storage::markUploaded(const QList<int> &ids)
{
    return updateStatus(ids, QStringLiteral("uploaded"), QString(), true);
}

bool Storage::markFailed(const QList<int> &ids, const QString &error)
{
    return updateStatus(ids, QStringLiteral("failed"), error, false);
}

bool Storage::markPending(int id)
{
    return updateStatus(QList<int>() << id, QStringLiteral("pending"), QString(), false);
}

bool Storage::updateStatus(const QList<int> &ids, const QString &status, const QString &error, bool setUploaded)
{
    if (ids.isEmpty()) {
        return true;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    foreach (int id, ids) {
        QString oldStatus;
        QSqlQuery current(m_db);
        current.prepare(QStringLiteral("select upload_status from reports where id = ?"));
        current.addBindValue(id);
        if (!current.exec()) {
            m_lastError = current.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!current.next()) {
            continue;
        }
        oldStatus = current.value(0).toString();

        QSqlQuery query(m_db);
        if (status == QStringLiteral("failed")) {
            query.prepare(QStringLiteral("update reports set upload_status = ?, retry_count = retry_count + 1, "
                                         "last_error = ? where id = ?"));
            query.addBindValue(status);
            query.addBindValue(error);
            query.addBindValue(id);
        } else if (setUploaded) {
            query.prepare(QStringLiteral("update reports set upload_status = ?, last_error = '', "
                                         "uploaded_at_ms = ? where id = ?"));
            query.addBindValue(status);
            query.addBindValue(QDateTime::currentMSecsSinceEpoch());
            query.addBindValue(id);
        } else {
            query.prepare(QStringLiteral("update reports set upload_status = ?, last_error = ? where id = ?"));
            query.addBindValue(status);
            query.addBindValue(error);
            query.addBindValue(id);
        }
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_db.rollback();
            return false;
        }
        if (!updateMapCellsForStatus(m_db, id, oldStatus, status, &m_lastError)) {
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    emit changed();
    return true;
}

bool Storage::deleteReports(const QList<int> &ids)
{
    if (ids.isEmpty()) {
        return true;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QList<MapCellKey> affectedCells;
    QSet<QString> seenCells;
    foreach (int id, ids) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("select zoom, q, r from map_report_cells where report_id = ?"));
        query.addBindValue(id);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_db.rollback();
            return false;
        }
        while (query.next()) {
            MapCellKey key;
            key.zoom = query.value(0).toInt();
            key.q = query.value(1).toInt();
            key.r = query.value(2).toInt();
            const QString cellId = keyString(key);
            if (!seenCells.contains(cellId)) {
                seenCells.insert(cellId);
                affectedCells.append(key);
            }
        }
    }

    const QStringList childTables = QStringList()
            << QStringLiteral("wifi_observations")
            << QStringLiteral("cell_observations")
            << QStringLiteral("ble_observations");

    foreach (const QString &table, childTables) {
        foreach (int id, ids) {
            QSqlQuery query(m_db);
            query.prepare(QStringLiteral("delete from %1 where report_id = ?").arg(table));
            query.addBindValue(id);
            if (!query.exec()) {
                m_lastError = query.lastError().text();
                m_db.rollback();
                return false;
            }
        }
    }

    foreach (int id, ids) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("delete from reports where id = ?"));
        query.addBindValue(id);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    foreach (int id, ids) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("delete from map_report_cells where report_id = ?"));
        query.addBindValue(id);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    foreach (const MapCellKey &key, affectedCells) {
        if (!recomputeMapCell(m_db, key, &m_lastError)) {
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    emit changed();
    return true;
}

QList<WifiObservation> Storage::wifiForReport(int reportId) const
{
    QList<WifiObservation> observations;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select mac_address, frequency, signal_strength, seen_ms "
                                 "from wifi_observations where report_id = ? order by id"));
    query.addBindValue(reportId);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return observations;
    }
    while (query.next()) {
        WifiObservation wifi;
        wifi.macAddress = query.value(0).toString();
        wifi.frequency = query.value(1).toInt();
        wifi.signalStrength = query.value(2).toInt();
        wifi.seenMs = query.value(3).toLongLong();
        observations.append(wifi);
    }
    return observations;
}

QList<CellObservation> Storage::cellsForReport(int reportId) const
{
    QList<CellObservation> observations;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select radio_type, mcc, mnc, lac, cell_id, psc, signal_strength, serving, seen_ms "
                                 "from cell_observations where report_id = ? order by id"));
    query.addBindValue(reportId);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return observations;
    }
    while (query.next()) {
        CellObservation cell;
        cell.radioType = query.value(0).toString();
        cell.mobileCountryCode = query.value(1).toInt();
        cell.mobileNetworkCode = query.value(2).toInt();
        cell.locationAreaCode = query.value(3).toInt();
        cell.cellId = query.value(4).toInt();
        cell.primaryScramblingCode = query.value(5).toInt();
        cell.signalStrength = query.value(6).toInt();
        cell.serving = query.value(7).toInt() != 0;
        cell.seenMs = query.value(8).toLongLong();
        observations.append(cell);
    }
    return observations;
}

QList<BleObservation> Storage::bleForReport(int reportId) const
{
    QList<BleObservation> observations;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select mac_address, address_type, name, signal_strength, beacon_type, "
                                 "id1, id2, id3, uuids, manufacturer_data, service_data, seen_ms "
                                 "from ble_observations where report_id = ? order by id"));
    query.addBindValue(reportId);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return observations;
    }
    while (query.next()) {
        BleObservation ble;
        ble.macAddress = query.value(0).toString();
        ble.addressType = query.value(1).toString();
        ble.name = query.value(2).toString();
        ble.signalStrength = query.value(3).toInt();
        ble.beaconType = query.value(4).toInt();
        ble.id1 = query.value(5).toString();
        ble.id2 = query.value(6).toString();
        ble.id3 = query.value(7).toString();
        ble.uuids = jsonToStringList(query.value(8).toString());
        ble.manufacturerData = query.value(9).toString();
        ble.serviceData = query.value(10).toString();
        ble.seenMs = query.value(11).toLongLong();
        observations.append(ble);
    }
    return observations;
}

QVariantMap Storage::reportSummaryToMap(const Report &report)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), report.id);
    map.insert(QStringLiteral("timestampMs"), report.timestampMs);
    map.insert(QStringLiteral("latitude"), report.position.latitude);
    map.insert(QStringLiteral("longitude"), report.position.longitude);
    map.insert(QStringLiteral("accuracy"), report.position.accuracy);
    map.insert(QStringLiteral("mode"), report.mode);
    map.insert(QStringLiteral("uploadStatus"), report.uploadStatus);
    map.insert(QStringLiteral("retryCount"), report.retryCount);
    map.insert(QStringLiteral("lastError"), report.lastError);
    map.insert(QStringLiteral("wifiCount"), report.wifi.count());
    map.insert(QStringLiteral("cellCount"), report.cells.count());
    map.insert(QStringLiteral("bleCount"), report.ble.count());
    return map;
}

QVariantMap Storage::reportToMap(const Report &report)
{
    QVariantMap map = reportSummaryToMap(report);
    map.insert(QStringLiteral("altitude"), report.position.altitude);
    map.insert(QStringLiteral("wifiEnabled"), report.wifiEnabled);
    map.insert(QStringLiteral("cellEnabled"), report.cellEnabled);
    map.insert(QStringLiteral("bleEnabled"), report.bleEnabled);
    map.insert(QStringLiteral("endpoint"), report.endpoint);
    map.insert(QStringLiteral("uploadedAtMs"), report.uploadedAtMs);
    map.insert(QStringLiteral("wifi"), wifiToList(report.wifi));
    map.insert(QStringLiteral("cells"), cellsToList(report.cells));
    map.insert(QStringLiteral("ble"), bleToList(report.ble));
    return map;
}
