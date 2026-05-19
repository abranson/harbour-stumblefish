#include "service.h"

#include "constants.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <QGeoCoordinate>
#include <QSet>

namespace {

const qint64 DuplicateHeartbeatMs = 15 * 60 * 1000;
const double MinimumDuplicateDistanceMeters = 30.0;

QSet<QString> wifiFingerprint(const Report &report)
{
    QSet<QString> keys;
    foreach (const WifiObservation &wifi, report.wifi) {
        if (!wifi.macAddress.isEmpty()) {
            keys.insert(wifi.macAddress.toLower());
        }
    }
    return keys;
}

QSet<QString> cellFingerprint(const Report &report)
{
    QSet<QString> keys;
    foreach (const CellObservation &cell, report.cells) {
        keys.insert(cell.radioType + QStringLiteral(":")
                    + QString::number(cell.mobileCountryCode) + QStringLiteral(":")
                    + QString::number(cell.mobileNetworkCode) + QStringLiteral(":")
                    + QString::number(cell.locationAreaCode) + QStringLiteral(":")
                    + QString::number(cell.cellId));
    }
    return keys;
}

QSet<QString> bleFingerprint(const Report &report)
{
    QSet<QString> keys;
    foreach (const BleObservation &ble, report.ble) {
        QString key = ble.macAddress.toLower();
        if (!ble.id1.isEmpty() || !ble.id2.isEmpty() || !ble.id3.isEmpty()) {
            key += QStringLiteral(":") + ble.id1 + QStringLiteral(":") + ble.id2 + QStringLiteral(":") + ble.id3;
        }
        if (!key.isEmpty()) {
            keys.insert(key);
        }
    }
    return keys;
}

bool setChanged(const QSet<QString> &a, const QSet<QString> &b)
{
    return a != b;
}

bool accuracyImprovedMeaningfully(const Report &report, const Report &previous)
{
    if (report.position.accuracy < 0.0 || previous.position.accuracy < 0.0) {
        return false;
    }

    const double improvement = previous.position.accuracy - report.position.accuracy;
    const double threshold = qMax(15.0, previous.position.accuracy * 0.25);
    return improvement >= threshold;
}

}

Service::Service(QObject *parent)
    : QObject(parent)
    , m_uploader(&m_storage, &m_settings, this)
{
    qRegisterMetaType<PositionFix>("PositionFix");

    if (!m_storage.open()) {
        m_lastMessage = QStringLiteral("Storage error: ") + m_storage.lastError();
    }

    connect(&m_settings, SIGNAL(changed()), this, SLOT(applySettings()));
    connect(&m_position, SIGNAL(fixReceived(PositionFix)), this, SLOT(positionFixReceived(PositionFix)));
    connect(&m_position, SIGNAL(statusChanged()), this, SLOT(emitStatus()));
    connect(&m_wifi, SIGNAL(changed()), this, SLOT(emitStatus()));
    connect(&m_cell, SIGNAL(changed()), this, SLOT(sourceStateChanged()));
    connect(&m_ble, SIGNAL(changed()), this, SLOT(emitStatus()));
    connect(&m_storage, SIGNAL(changed()), this, SLOT(storageChanged()));
    connect(&m_uploader, SIGNAL(uploadFinished(bool,QString)), this, SLOT(uploaderFinished(bool,QString)));

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QString::fromLatin1(Stumblefish::ServiceName))) {
        qWarning() << "Failed to register D-Bus service" << bus.lastError().message();
    }
    if (!bus.registerObject(QString::fromLatin1(Stumblefish::ObjectPath), this,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qWarning() << "Failed to register D-Bus object" << bus.lastError().message();
    }

    applySettings();
}

QVariantMap Service::status() const
{
    QVariantMap map;
    map.insert(QStringLiteral("running"), true);
    map.insert(QStringLiteral("mode"), m_settings.mode());
    map.insert(QStringLiteral("positionStatus"), m_position.status());
    map.insert(QStringLiteral("locationEnabled"), m_position.locationEnabled());
    map.insert(QStringLiteral("wifiStatus"), m_wifi.status());
    map.insert(QStringLiteral("cellStatus"), m_cell.status());
    map.insert(QStringLiteral("cellAvailable"), m_cell.available());
    map.insert(QStringLiteral("cellUnavailableReason"), m_cell.unavailableReason());
    map.insert(QStringLiteral("bleStatus"), m_ble.status());
    map.insert(QStringLiteral("uploading"), m_uploader.uploading());
    map.insert(QStringLiteral("message"), m_lastMessage);
    map.insert(QStringLiteral("lastCollectedMs"), m_storage.lastReportTimestamp());
    map.insert(QStringLiteral("counts"), m_storage.counts());

    const PositionFix fix = m_position.lastFix();
    map.insert(QStringLiteral("hasFix"), fix.valid);
    map.insert(QStringLiteral("gnssBackedFix"), m_position.hasGnssFix(fix.timestampMs));
    map.insert(QStringLiteral("satellitesInUse"), m_position.satellitesInUse());
    map.insert(QStringLiteral("latitude"), fix.latitude);
    map.insert(QStringLiteral("longitude"), fix.longitude);
    map.insert(QStringLiteral("accuracy"), fix.accuracy);
    map.insert(QStringLiteral("fixTimestampMs"), fix.timestampMs);
    return map;
}

QVariantMap Service::settings() const
{
    return m_settings.toMap();
}

QVariantList Service::reports(int limit) const
{
    QVariantList list;
    foreach (const Report &item, m_storage.unuploadedReports(limit > 0 ? limit : 100)) {
        list.append(Storage::reportSummaryToMap(item));
    }
    return list;
}

QVariantMap Service::report(int id) const
{
    return Storage::reportToMap(m_storage.report(id));
}

void Service::setSetting(const QString &key, const QDBusVariant &value)
{
    m_settings.setValue(key, value.variant());
}

void Service::collectNow()
{
    collectReport(m_position.lastFix(), QStringLiteral("manual"));
}

void Service::uploadPending()
{
    m_uploader.uploadPending();
}

void Service::retryReport(int reportId)
{
    m_uploader.retryReport(reportId);
}

void Service::deleteReport(int reportId)
{
    if (m_uploader.uploading()) {
        m_lastMessage = QStringLiteral("Wait for the current upload before deleting reports");
        emitStatus();
        return;
    }

    if (m_storage.report(reportId).id <= 0) {
        m_lastMessage = QStringLiteral("Report not found");
        emitStatus();
        return;
    }

    if (!m_storage.deleteReport(reportId)) {
        m_lastMessage = QStringLiteral("Storage error: ") + m_storage.lastError();
        emitStatus();
        return;
    }

    m_lastMessage = QStringLiteral("Deleted report %1").arg(reportId);
    emitStatus();
}

void Service::clearPendingReports()
{
    if (m_uploader.uploading()) {
        m_lastMessage = QStringLiteral("Wait for the current upload before clearing reports");
        emitStatus();
        return;
    }

    const int count = m_storage.clearPendingReports();
    if (count < 0) {
        m_lastMessage = QStringLiteral("Storage error: ") + m_storage.lastError();
    } else if (count == 0) {
        m_lastMessage = QStringLiteral("No pending reports to clear");
    } else {
        m_lastMessage = QStringLiteral("Cleared %1 pending reports").arg(count);
    }
    emitStatus();
}

void Service::applySettings()
{
    m_wifi.setEnabled(m_settings.wifiEnabled());
    m_cell.setEnabled(m_settings.cellEnabled());
    m_ble.setEnabled(m_settings.bleEnabled());
    m_position.setActive(m_settings.mode() == QStringLiteral("active") && anySourceEnabled());

    if (!anySourceEnabled()) {
        m_lastMessage = m_settings.cellEnabled() && !m_cell.available()
                ? QStringLiteral("Cell collection unavailable: ") + m_cell.unavailableReason()
                : QStringLiteral("No data sources enabled");
    } else if (m_lastMessage == QStringLiteral("No data sources enabled")
               || m_lastMessage.startsWith(QStringLiteral("Cell collection unavailable:"))) {
        m_lastMessage.clear();
    }

    emit settingsChanged(m_settings.toMap());
    emitStatus();
}

void Service::sourceStateChanged()
{
    m_position.setActive(m_settings.mode() == QStringLiteral("active") && anySourceEnabled());
    if (!anySourceEnabled()) {
        m_lastMessage = m_settings.cellEnabled() && !m_cell.available()
                ? QStringLiteral("Cell collection unavailable: ") + m_cell.unavailableReason()
                : QStringLiteral("No data sources enabled");
    } else if (m_lastMessage == QStringLiteral("No data sources enabled")
               || m_lastMessage.startsWith(QStringLiteral("Cell collection unavailable:"))) {
        m_lastMessage.clear();
    }
    emitStatus();
}

void Service::positionFixReceived(const PositionFix &fix)
{
    collectReport(fix, QStringLiteral("position"));
}

void Service::storageChanged()
{
    emit reportsChanged();
    emitStatus();
}

void Service::uploaderFinished(bool success, const QString &message)
{
    Q_UNUSED(success);
    m_lastMessage = message;
    emit uploadFinished(success, message);
    emitStatus();
}

bool Service::anySourceEnabled() const
{
    return m_settings.wifiEnabled()
            || (m_settings.cellEnabled() && m_cell.available())
            || m_settings.bleEnabled();
}

bool Service::collectReport(const PositionFix &fix, const QString &reason)
{
    if (!anySourceEnabled()) {
        m_lastMessage = m_settings.cellEnabled() && !m_cell.available()
                ? QStringLiteral("Cell collection unavailable: ") + m_cell.unavailableReason()
                : QStringLiteral("No data sources enabled");
        emitStatus();
        return false;
    }

    if (!m_position.locationEnabled()) {
        m_lastMessage = QStringLiteral("Location is disabled in device settings");
        emitStatus();
        return false;
    }

    if (!fix.valid || fix.accuracy < 0.0 || fix.accuracy > 100.0) {
        m_lastMessage = QStringLiteral("Waiting for an accurate position fix");
        emitStatus();
        return false;
    }

    if (!m_position.hasGnssFix(fix.timestampMs)) {
        m_lastMessage = QStringLiteral("Waiting for a GNSS-backed position fix");
        emitStatus();
        return false;
    }

    Report report;
    report.timestampMs = QDateTime::currentMSecsSinceEpoch();
    report.position = fix;
    report.mode = m_settings.mode();
    report.wifiEnabled = m_settings.wifiEnabled();
    report.cellEnabled = m_settings.cellEnabled() && m_cell.available();
    report.bleEnabled = m_settings.bleEnabled();
    report.endpoint = m_settings.endpoint();

    if (report.wifiEnabled) {
        report.wifi = m_wifi.observations();
    }
    if (report.cellEnabled) {
        report.cells = m_cell.observations();
    }
    if (report.bleEnabled) {
        report.ble = m_ble.observations();
    }

    if (report.cells.isEmpty() && report.ble.isEmpty() && report.wifi.count() < 2) {
        m_lastMessage = QStringLiteral("Not enough radio observations for a report");
        emitStatus();
        return false;
    }

    if (isDuplicateReport(report)) {
        m_lastMessage = reason == QStringLiteral("manual")
                ? QStringLiteral("Skipped duplicate report")
                : QStringLiteral("Duplicate report suppressed");
        emitStatus();
        return false;
    }

    const int id = m_storage.addReport(report);
    if (id <= 0) {
        m_lastMessage = QStringLiteral("Storage error: ") + m_storage.lastError();
        emitStatus();
        return false;
    }

    m_lastMessage = QStringLiteral("Collected report %1").arg(id);

    if (m_settings.autoUploadEnabled()) {
        m_uploader.uploadPending();
    }

    emitStatus();
    return true;
}

bool Service::isDuplicateReport(const Report &report) const
{
    const QList<Report> recent = m_storage.recentReports(1);
    if (recent.isEmpty()) {
        return false;
    }

    const Report previous = recent.first();
    if (!previous.position.valid) {
        return false;
    }

    const qint64 elapsed = report.timestampMs - previous.timestampMs;
    if (elapsed >= DuplicateHeartbeatMs) {
        return false;
    }

    const QGeoCoordinate previousCoordinate(previous.position.latitude, previous.position.longitude);
    const QGeoCoordinate currentCoordinate(report.position.latitude, report.position.longitude);
    const double distance = previousCoordinate.distanceTo(currentCoordinate);
    const double distanceThreshold = qMax(MinimumDuplicateDistanceMeters,
                                          qMax(previous.position.accuracy, report.position.accuracy));
    if (distance > distanceThreshold) {
        return false;
    }

    if (accuracyImprovedMeaningfully(report, previous)) {
        return false;
    }

    if (setChanged(wifiFingerprint(report), wifiFingerprint(previous))) {
        return false;
    }
    if (setChanged(cellFingerprint(report), cellFingerprint(previous))) {
        return false;
    }
    if (setChanged(bleFingerprint(report), bleFingerprint(previous))) {
        return false;
    }

    return true;
}

void Service::emitStatus()
{
    emit statusChanged(status());
}
