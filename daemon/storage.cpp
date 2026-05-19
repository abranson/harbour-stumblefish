#include "storage.h"

#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariantList>

namespace {

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
        map.insert(QStringLiteral("ssid"), wifi.ssid);
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
                               ")"));
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
        return 0;
    }

    const int reportId = query.lastInsertId().toInt();

    foreach (const WifiObservation &wifi, report.wifi) {
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("insert into wifi_observations "
                                      "(report_id, mac_address, ssid, frequency, signal_strength, seen_ms) "
                                      "values (?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(reportId);
        insert.addBindValue(wifi.macAddress);
        insert.addBindValue(wifi.ssid);
        insert.addBindValue(wifi.frequency);
        insert.addBindValue(wifi.signalStrength);
        insert.addBindValue(wifi.seenMs);
        if (!insert.exec()) {
            m_lastError = insert.lastError().text();
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
        }
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

QList<Report> Storage::uploadCandidates(int limit) const
{
    QList<Report> reports;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("select id from reports "
                                 "where upload_status in ('pending', 'failed') "
                                 "order by id asc limit ?"));
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
    foreach (int id, ids) {
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
            return false;
        }
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
    query.prepare(QStringLiteral("select mac_address, ssid, frequency, signal_strength, seen_ms "
                                 "from wifi_observations where report_id = ? order by id"));
    query.addBindValue(reportId);
    if (!query.exec()) {
        const_cast<Storage *>(this)->m_lastError = query.lastError().text();
        return observations;
    }
    while (query.next()) {
        WifiObservation wifi;
        wifi.macAddress = query.value(0).toString();
        wifi.ssid = query.value(1).toString();
        wifi.frequency = query.value(2).toInt();
        wifi.signalStrength = query.value(3).toInt();
        wifi.seenMs = query.value(4).toLongLong();
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
