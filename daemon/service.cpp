#include "service.h"

#include "constants.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDebug>
#include <QGeoCoordinate>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <networkmanager.h>
#include <networkservice.h>

#include <climits>

namespace {

const qint64 DuplicateHeartbeatMs = 15 * 60 * 1000;
const qint64 AutoUploadIntervalMs = 8 * 60 * 60 * 1000;
const qint64 AutoUploadNetworkRetryMs = 15 * 60 * 1000;
const int AutoUploadMaxRetries = 5;
const qint64 PruneIntervalMs = 24 * 60 * 60 * 1000;
const int AppLifecycleQuitDelayMs = 3000;
const double MinimumDuplicateDistanceMeters = 30.0;
const char ServiceUnitName[] = "harbour-stumblefishd.service";

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
    , m_networkManager(new NetworkManager(this))
    , m_uploader(&m_storage, &m_settings, this)
    , m_clientWatcher(this)
    , m_quitWhenIdle(false)
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
    connect(m_networkManager, SIGNAL(connectedChanged()), this, SLOT(scheduleAutoUpload()));
    connect(m_networkManager, SIGNAL(defaultRouteChanged(NetworkService*)), this, SLOT(scheduleAutoUpload()));
    connect(m_networkManager, SIGNAL(connectedWifiChanged()), this, SLOT(scheduleAutoUpload()));
    connect(&m_autoUploadTimer, SIGNAL(timeout()), this, SLOT(autoUploadDueReports()));
    connect(&m_pruneTimer, SIGNAL(timeout()), this, SLOT(pruneDueReports()));
    connect(&m_lifecycleQuitTimer, SIGNAL(timeout()), this, SLOT(quitForAppLifecycle()));
    connect(&m_clientWatcher, SIGNAL(serviceUnregistered(QString)),
            this, SLOT(clientServiceUnregistered(QString)));
    m_lifecycleQuitTimer.setSingleShot(true);

    QDBusConnection bus = QDBusConnection::sessionBus();
    m_clientWatcher.setConnection(bus);
    m_clientWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    if (!bus.registerService(QString::fromLatin1(Stumblefish::ServiceName))) {
        qWarning() << "Failed to register D-Bus service" << bus.lastError().message();
    }
    if (!bus.registerObject(QString::fromLatin1(Stumblefish::ObjectPath), this,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qWarning() << "Failed to register D-Bus object" << bus.lastError().message();
    }

    applySettings();
    m_pruneTimer.start(60 * 60 * 1000);
    QTimer::singleShot(0, this, SLOT(pruneDueReports()));
}

QVariantMap Service::status() const
{
    QVariantMap map;
    const QString backgroundMode = m_settings.mode();
    map.insert(QStringLiteral("running"), true);
    map.insert(QStringLiteral("mode"), effectiveMode());
    map.insert(QStringLiteral("backgroundMode"), backgroundMode);
    map.insert(QStringLiteral("positionStatus"), m_position.status());
    map.insert(QStringLiteral("locationEnabled"), m_position.locationEnabled());
    map.insert(QStringLiteral("wifiStatus"), m_wifi.status());
    map.insert(QStringLiteral("cellStatus"), m_cell.status());
    map.insert(QStringLiteral("cellAvailable"), m_cell.available());
    map.insert(QStringLiteral("cellUnavailableReason"), m_cell.unavailableReason());
    map.insert(QStringLiteral("bleStatus"), m_ble.status());
    map.insert(QStringLiteral("uploading"), m_uploader.uploading());
    const QString stateMessage = collectionStateMessage();
    map.insert(QStringLiteral("collectionStateMessage"),
               stateMessage.isEmpty()
               ? QStringLiteral("Collector status unavailable")
               : stateMessage);
    map.insert(QStringLiteral("message"), m_lastMessage);
    map.insert(QStringLiteral("lastCollectedMs"), m_storage.lastReportTimestamp());
    map.insert(QStringLiteral("counts"), m_storage.counts());
    QString autoUploadNetworkReason;
    map.insert(QStringLiteral("autoUploadNetworkAllowed"),
               autoUploadNetworkAllowed(&autoUploadNetworkReason));
    map.insert(QStringLiteral("autoUploadNetworkReason"), autoUploadNetworkReason);
    map.insert(QStringLiteral("autoUploadIntervalMs"), AutoUploadIntervalMs);
    map.insert(QStringLiteral("lastAutoUploadMs"), m_settings.lastAutoUploadMs());
    map.insert(QStringLiteral("nextAutoUploadMs"),
               m_settings.autoUploadEnabled() && m_settings.lastAutoUploadMs() > 0
               ? m_settings.lastAutoUploadMs() + AutoUploadIntervalMs : 0);
    map.insert(QStringLiteral("appOpenClientCount"), m_appClients.count());

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

QVariantMap Service::mapSummary() const
{
    return m_storage.mapSummary();
}

QVariantList Service::mapCells(double minLatitude, double minLongitude,
                               double maxLatitude, double maxLongitude, int zoom) const
{
    return m_storage.mapCells(minLatitude, minLongitude, maxLatitude, maxLongitude, zoom);
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

void Service::appOpened()
{
    addAppClient(calledFromDBus() ? message().service() : QString());
}

void Service::appClosed()
{
    removeAppClient(calledFromDBus() ? message().service() : QString());
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

int Service::pruneReports()
{
    if (m_uploader.uploading()) {
        m_lastMessage = QStringLiteral("Wait for the current upload before pruning reports");
        emitStatus();
        return -1;
    }

    const int count = pruneReportsWithRetention(true);
    if (count >= 0) {
        m_settings.setLastPruneMs(QDateTime::currentMSecsSinceEpoch());
    }
    emitStatus();
    return count;
}

void Service::applySettings()
{
    syncUserServiceEnabled();

    const bool allowed = collectionAllowed();
    m_wifi.setEnabled(allowed && m_settings.wifiEnabled());
    m_cell.setEnabled(allowed && m_settings.cellEnabled());
    m_ble.setEnabled(allowed && m_settings.bleEnabled());
    m_position.setActive(positionShouldBeActive());
    scheduleAutoUpload();

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
    maybeQuitForAppLifecycle();
}

void Service::sourceStateChanged()
{
    m_position.setActive(positionShouldBeActive());
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
    scheduleAutoUpload();
    emitStatus();
}

void Service::uploaderFinished(bool success, const QString &message)
{
    Q_UNUSED(success);
    m_lastMessage = message;
    emit uploadFinished(success, message);
    if (m_quitWhenIdle) {
        emitStatus();
        maybeQuitForAppLifecycle();
        return;
    }
    scheduleAutoUpload();
    emitStatus();
}

void Service::autoUploadDueReports()
{
    if (!m_settings.autoUploadEnabled()) {
        m_autoUploadTimer.stop();
        return;
    }

    if (m_uploader.uploading()) {
        m_autoUploadTimer.start(60 * 1000);
        return;
    }

    QString reason;
    if (!autoUploadNetworkAllowed(&reason)) {
        m_autoUploadTimer.start(AutoUploadNetworkRetryMs);
        emitStatus();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_settings.setLastAutoUploadMs(now);

    if (m_storage.uploadCandidates(1, AutoUploadMaxRetries).isEmpty()) {
        scheduleAutoUpload();
        emitStatus();
        return;
    }

    m_uploader.uploadAutomatically();
    scheduleAutoUpload();
}

void Service::scheduleAutoUpload()
{
    if (!m_settings.autoUploadEnabled()) {
        m_autoUploadTimer.stop();
        return;
    }

    qint64 last = m_settings.lastAutoUploadMs();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (last <= 0) {
        last = now;
        m_settings.setLastAutoUploadMs(last);
    }

    const qint64 next = last + AutoUploadIntervalMs;
    qint64 delay = qMax<qint64>(1000, next - now);
    if (next <= now && !autoUploadNetworkAllowed()) {
        delay = AutoUploadNetworkRetryMs;
    }

    m_autoUploadTimer.start(qMin<qint64>(delay, INT_MAX));
}

void Service::pruneDueReports()
{
    if (m_uploader.uploading() || m_settings.reportRetentionDays() < 0) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_settings.lastPruneMs() > 0 && now - m_settings.lastPruneMs() < PruneIntervalMs) {
        return;
    }

    const int count = pruneReportsWithRetention(false);
    if (count >= 0) {
        m_settings.setLastPruneMs(now);
    }
    if (count != 0) {
        emitStatus();
    }
}

bool Service::anySourceEnabled() const
{
    return m_settings.wifiEnabled()
            || (m_settings.cellEnabled() && m_cell.available())
            || m_settings.bleEnabled();
}

bool Service::collectionAllowed() const
{
    return m_settings.allowBackgroundDaemon() || !m_appClients.isEmpty();
}

QString Service::effectiveMode() const
{
    return !m_appClients.isEmpty() ? QStringLiteral("active") : m_settings.mode();
}

bool Service::positionShouldBeActive() const
{
    return collectionAllowed()
            && effectiveMode() == QStringLiteral("active")
            && anySourceEnabled();
}

void Service::syncUserServiceEnabled() const
{
    QStringList arguments;
    arguments << QStringLiteral("--user")
              << (m_settings.allowBackgroundDaemon()
                  ? QStringLiteral("enable")
                  : QStringLiteral("disable"))
              << QString::fromLatin1(ServiceUnitName);

    if (!QProcess::startDetached(QStringLiteral("systemctl"), arguments)) {
        qWarning() << "Failed to update user service enable state" << arguments;
    }
}

QString Service::collectionStateMessage() const
{
    if (!collectionAllowed()) {
        return QStringLiteral("Collector idle");
    }

    if (!anySourceEnabled()) {
        return m_settings.cellEnabled() && !m_cell.available()
                ? QStringLiteral("Cell collection unavailable: ") + m_cell.unavailableReason()
                : QStringLiteral("No data sources enabled");
    }

    if (!m_position.locationEnabled()) {
        return QStringLiteral("Location disabled");
    }

    const QString positionStatus = m_position.status();
    if (positionStatus == QStringLiteral("no-position-source")) {
        return QStringLiteral("No position source");
    }
    if (positionStatus == QStringLiteral("error")) {
        return QStringLiteral("Location error");
    }

    const PositionFix fix = m_position.lastFix();
    if (!fix.valid) {
        return effectiveMode() == QStringLiteral("passive")
                ? QStringLiteral("Waiting for app location fixes")
                : QStringLiteral("Waiting for location lock");
    }

    if (!m_position.hasGnssFix(fix.timestampMs)) {
        return QStringLiteral("Waiting for GNSS lock");
    }

    return QStringLiteral("Gathering reports");
}

bool Service::autoUploadNetworkAllowed(QString *reason) const
{
    if (!m_networkManager || !m_networkManager->connected()) {
        if (reason) {
            *reason = QStringLiteral("No network connection");
        }
        return false;
    }

    if (m_settings.uploadOnNonWifi()) {
        if (reason) {
            *reason = QStringLiteral("Connected");
        }
        return true;
    }

    NetworkService *wifi = m_networkManager->connectedWifi();
    if (wifi && wifi->connected()) {
        if (reason) {
            *reason = QStringLiteral("Wi-Fi connected");
        }
        return true;
    }

    NetworkService *route = m_networkManager->defaultRoute();
    if (route && route->type() == QStringLiteral("wifi") && route->connected()) {
        if (reason) {
            *reason = QStringLiteral("Wi-Fi connected");
        }
        return true;
    }

    if (reason) {
        *reason = QStringLiteral("Waiting for Wi-Fi");
    }
    return false;
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
    report.mode = effectiveMode();
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
        Q_UNUSED(reason);
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

int Service::pruneReportsWithRetention(bool manual)
{
    const int retentionDays = m_settings.reportRetentionDays();
    if (retentionDays < 0) {
        if (manual) {
            m_lastMessage = QStringLiteral("Report retention is disabled");
        }
        return 0;
    }

    const qint64 cutoffMs = QDateTime::currentMSecsSinceEpoch()
            - static_cast<qint64>(retentionDays) * 24 * 60 * 60 * 1000;
    const int count = m_storage.pruneReportsOlderThan(cutoffMs);
    if (count < 0) {
        m_lastMessage = QStringLiteral("Storage error: ") + m_storage.lastError();
    } else if (manual) {
        m_lastMessage = count == 0
                ? QStringLiteral("No old reports to prune")
                : QStringLiteral("Pruned %1 old reports").arg(count);
    } else if (count > 0) {
        m_lastMessage = QStringLiteral("Pruned %1 old reports").arg(count);
    }
    return count;
}

void Service::emitStatus()
{
    emit statusChanged(status());
}

void Service::clientServiceUnregistered(const QString &serviceName)
{
    removeAppClient(serviceName);
}

void Service::quitForAppLifecycle()
{
    if (m_settings.allowBackgroundDaemon() || !m_appClients.isEmpty() || m_uploader.uploading()) {
        return;
    }

    QCoreApplication::quit();
}

void Service::addAppClient(const QString &serviceName)
{
    if (serviceName.isEmpty()) {
        return;
    }

    const bool wasEmpty = m_appClients.isEmpty();
    m_appClients.insert(serviceName);
    m_clientWatcher.addWatchedService(serviceName);
    m_lifecycleQuitTimer.stop();
    m_quitWhenIdle = false;

    if (wasEmpty) {
        applySettings();
    } else {
        emitStatus();
    }
}

void Service::removeAppClient(const QString &serviceName)
{
    const bool hadClients = !m_appClients.isEmpty();
    if (!serviceName.isEmpty()) {
        m_appClients.remove(serviceName);
        m_clientWatcher.removeWatchedService(serviceName);
    }

    if (hadClients && m_appClients.isEmpty()) {
        applySettings();
        return;
    }

    emitStatus();
    maybeQuitForAppLifecycle();
}

void Service::maybeQuitForAppLifecycle()
{
    if (m_settings.allowBackgroundDaemon() || !m_appClients.isEmpty()) {
        m_lifecycleQuitTimer.stop();
        m_quitWhenIdle = false;
        return;
    }

    m_quitWhenIdle = true;
    m_position.setActive(false);
    m_wifi.setEnabled(false);
    m_cell.setEnabled(false);
    m_ble.setEnabled(false);
    m_autoUploadTimer.stop();

    if (m_uploader.uploading()) {
        m_lastMessage = QStringLiteral("Waiting for upload before stopping daemon");
        emitStatus();
        return;
    }

    if (!m_lifecycleQuitTimer.isActive()) {
        m_lifecycleQuitTimer.start(AppLifecycleQuitDelayMs);
    }
}
