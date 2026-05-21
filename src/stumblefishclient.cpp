#include "stumblefishclient.h"

#include "constants.h"

#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QtGlobal>

namespace {

QVariant normalizeVariant(const QVariant &value);

QVariantMap normalizeMap(const QVariantMap &map)
{
    QVariantMap normalized;
    QVariantMap::const_iterator it = map.constBegin();
    for (; it != map.constEnd(); ++it) {
        normalized.insert(it.key(), normalizeVariant(it.value()));
    }
    return normalized;
}

QVariantList normalizeList(const QVariantList &list)
{
    QVariantList normalized;
    foreach (const QVariant &item, list) {
        normalized.append(normalizeVariant(item));
    }
    return normalized;
}

QVariant normalizeVariant(const QVariant &value)
{
    if (value.canConvert<QDBusVariant>()) {
        return normalizeVariant(qvariant_cast<QDBusVariant>(value).variant());
    }

    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
        if (argument.currentType() == QDBusArgument::MapType) {
            QVariantMap map;
            argument >> map;
            return normalizeMap(map);
        }
        if (argument.currentType() == QDBusArgument::ArrayType) {
            QVariantList list;
            argument >> list;
            return normalizeList(list);
        }
    }

    if (value.type() == QVariant::Map) {
        return normalizeMap(value.toMap());
    }
    if (value.type() == QVariant::List || value.type() == QVariant::StringList) {
        return normalizeList(value.toList());
    }
    return value;
}

}

StumblefishClient::StumblefishClient(QObject *parent)
    : QObject(parent)
    , m_interface(new QDBusInterface(QString::fromLatin1(Stumblefish::ServiceName),
                                     QString::fromLatin1(Stumblefish::ObjectPath),
                                     QString::fromLatin1(Stumblefish::InterfaceName),
                                     QDBusConnection::sessionBus(),
                                     this))
    , m_busyCount(0)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(Stumblefish::ServiceName),
                QString::fromLatin1(Stumblefish::ObjectPath),
                QString::fromLatin1(Stumblefish::InterfaceName),
                QStringLiteral("statusChanged"),
                this,
                SLOT(handleStatusSignal(QVariantMap)));
    bus.connect(QString::fromLatin1(Stumblefish::ServiceName),
                QString::fromLatin1(Stumblefish::ObjectPath),
                QString::fromLatin1(Stumblefish::InterfaceName),
                QStringLiteral("settingsChanged"),
                this,
                SLOT(handleSettingsSignal(QVariantMap)));
    bus.connect(QString::fromLatin1(Stumblefish::ServiceName),
                QString::fromLatin1(Stumblefish::ObjectPath),
                QString::fromLatin1(Stumblefish::InterfaceName),
                QStringLiteral("reportsChanged"),
                this,
                SLOT(handleReportsSignal()));
    bus.connect(QString::fromLatin1(Stumblefish::ServiceName),
                QString::fromLatin1(Stumblefish::ObjectPath),
                QString::fromLatin1(Stumblefish::InterfaceName),
                QStringLiteral("uploadFinished"),
                this,
                SLOT(handleUploadFinished(bool,QString)));

    sendLifecycleMessage(QStringLiteral("appOpened"));
    refresh();
}

StumblefishClient::~StumblefishClient()
{
    sendLifecycleMessage(QStringLiteral("appClosed"));
}

QVariantMap StumblefishClient::status() const
{
    return m_status;
}

QVariantMap StumblefishClient::settings() const
{
    return m_settings;
}

QVariantList StumblefishClient::reports() const
{
    return m_reports;
}

QVariantMap StumblefishClient::selectedReport() const
{
    return m_selectedReport;
}

QVariantMap StumblefishClient::mapSummary() const
{
    return m_mapSummary;
}

QVariantList StumblefishClient::mapCells() const
{
    return m_mapCells;
}

bool StumblefishClient::busy() const
{
    return m_busyCount > 0;
}

QString StumblefishClient::message() const
{
    return m_message;
}

void StumblefishClient::refresh()
{
    asyncCall(QStringLiteral("status"), QVariantList(), QStringLiteral("status"));
    asyncCall(QStringLiteral("settings"), QVariantList(), QStringLiteral("settings"));
    asyncCall(QStringLiteral("reports"), QVariantList() << 100, QStringLiteral("reports"));
    refreshMapSummary();
}

void StumblefishClient::loadReport(int id)
{
    asyncCall(QStringLiteral("report"), QVariantList() << id, QStringLiteral("report"));
}

void StumblefishClient::setMode(const QString &mode)
{
    setSetting(QStringLiteral("mode"), mode);
}

void StumblefishClient::setSourceEnabled(const QString &source, bool enabled)
{
    if (source == QStringLiteral("wifi")) {
        setSetting(QStringLiteral("wifiEnabled"), enabled);
    } else if (source == QStringLiteral("cell")) {
        setSetting(QStringLiteral("cellEnabled"), enabled);
    } else if (source == QStringLiteral("ble")) {
        setSetting(QStringLiteral("bleEnabled"), enabled);
    }
}

void StumblefishClient::setEndpoint(const QString &endpoint)
{
    setSetting(QStringLiteral("endpoint"), endpoint);
}

void StumblefishClient::setMapTileUrlTemplate(const QString &mapTileUrlTemplate)
{
    setSetting(QStringLiteral("mapTileUrlTemplate"), mapTileUrlTemplate);
}

void StumblefishClient::setReportRetentionDays(int days)
{
    setSetting(QStringLiteral("reportRetentionDays"), days);
}

void StumblefishClient::setAutoUploadEnabled(bool enabled)
{
    setSetting(QStringLiteral("autoUploadEnabled"), enabled);
}

void StumblefishClient::setUploadOnNonWifi(bool enabled)
{
    setSetting(QStringLiteral("uploadOnNonWifi"), enabled);
}

void StumblefishClient::setAllowBackgroundDaemon(bool enabled)
{
    setSetting(QStringLiteral("allowBackgroundDaemon"), enabled);
}

void StumblefishClient::collectNow()
{
    asyncCall(QStringLiteral("collectNow"), QVariantList(), QStringLiteral("void"));
}

void StumblefishClient::uploadPending()
{
    asyncCall(QStringLiteral("uploadPending"), QVariantList(), QStringLiteral("void"));
}

void StumblefishClient::retryReport(int id)
{
    asyncCall(QStringLiteral("retryReport"), QVariantList() << id, QStringLiteral("void"));
}

void StumblefishClient::deleteReport(int id)
{
    asyncCall(QStringLiteral("deleteReport"), QVariantList() << id, QStringLiteral("void"));
}

void StumblefishClient::clearPendingReports()
{
    asyncCall(QStringLiteral("clearPendingReports"), QVariantList(), QStringLiteral("void"));
}

void StumblefishClient::pruneReports()
{
    asyncCall(QStringLiteral("pruneReports"), QVariantList(), QStringLiteral("prune"));
}

void StumblefishClient::refreshMapSummary()
{
    asyncCall(QStringLiteral("mapSummary"), QVariantList(), QStringLiteral("mapSummary"));
}

void StumblefishClient::requestMapCells(double minLatitude, double minLongitude,
                                        double maxLatitude, double maxLongitude, int zoom)
{
    asyncCall(QStringLiteral("mapCells"),
              QVariantList() << minLatitude << minLongitude << maxLatitude << maxLongitude << zoom,
              QStringLiteral("mapCells"));
}

void StumblefishClient::handleStatusSignal(const QVariantMap &status)
{
    m_status = normalizeMap(status);
    emit statusChanged();
    const QString daemonMessage = m_status.value(QStringLiteral("message")).toString();
    if (!daemonMessage.isEmpty()) {
        setMessage(daemonMessage);
    }
}

void StumblefishClient::handleSettingsSignal(const QVariantMap &settings)
{
    m_settings = normalizeMap(settings);
    emit settingsChanged();
}

void StumblefishClient::handleReportsSignal()
{
    asyncCall(QStringLiteral("reports"), QVariantList() << 100, QStringLiteral("reports"));
    refreshMapSummary();
}

void StumblefishClient::handleUploadFinished(bool success, const QString &message)
{
    Q_UNUSED(success);
    setMessage(message);
    refresh();
}

void StumblefishClient::pendingFinished(QDBusPendingCallWatcher *watcher)
{
    const QString kind = watcher->property("kind").toString();
    setBusyCount(m_busyCount - 1);

    if (watcher->isError()) {
        setMessage(watcher->error().message());
        watcher->deleteLater();
        return;
    }

    if (kind == QStringLiteral("status")) {
        QDBusPendingReply<QVariantMap> reply(*watcher);
        m_status = normalizeMap(reply.value());
        emit statusChanged();
    } else if (kind == QStringLiteral("settings")) {
        QDBusPendingReply<QVariantMap> reply(*watcher);
        m_settings = normalizeMap(reply.value());
        emit settingsChanged();
    } else if (kind == QStringLiteral("reports")) {
        QDBusPendingReply<QVariantList> reply(*watcher);
        m_reports = normalizeList(reply.value());
        emit reportsChanged();
    } else if (kind == QStringLiteral("report")) {
        QDBusPendingReply<QVariantMap> reply(*watcher);
        m_selectedReport = normalizeMap(reply.value());
        emit selectedReportChanged();
    } else if (kind == QStringLiteral("mapSummary")) {
        QDBusPendingReply<QVariantMap> reply(*watcher);
        m_mapSummary = normalizeMap(reply.value());
        emit mapSummaryChanged();
    } else if (kind == QStringLiteral("mapCells")) {
        QDBusPendingReply<QVariantList> reply(*watcher);
        m_mapCells = normalizeList(reply.value());
        emit mapCellsChanged();
    } else if (kind == QStringLiteral("prune")) {
        QDBusPendingReply<int> reply(*watcher);
        const int count = reply.value();
        if (count >= 0) {
            setMessage(count == 0
                       ? QStringLiteral("No old reports to prune")
                       : QStringLiteral("Pruned %1 old reports").arg(count));
        }
        refresh();
    }

    watcher->deleteLater();
}

void StumblefishClient::asyncCall(const QString &method, const QVariantList &arguments, const QString &kind)
{
    QDBusPendingCall call = m_interface->asyncCallWithArgumentList(method, arguments);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    watcher->setProperty("kind", kind);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            this, SLOT(pendingFinished(QDBusPendingCallWatcher*)));
    setBusyCount(m_busyCount + 1);
}

void StumblefishClient::sendLifecycleMessage(const QString &method)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
                QString::fromLatin1(Stumblefish::ServiceName),
                QString::fromLatin1(Stumblefish::ObjectPath),
                QString::fromLatin1(Stumblefish::InterfaceName),
                method);
    QDBusConnection::sessionBus().send(message);
}

void StumblefishClient::setBusyCount(int count)
{
    const bool wasBusy = busy();
    m_busyCount = qMax(0, count);
    if (wasBusy != busy()) {
        emit busyChanged();
    }
}

void StumblefishClient::setMessage(const QString &message)
{
    if (m_message == message) {
        return;
    }
    m_message = message;
    emit messageChanged();
}

void StumblefishClient::setSetting(const QString &key, const QVariant &value)
{
    QVariantList arguments;
    arguments << key << QVariant::fromValue(QDBusVariant(value));
    asyncCall(QStringLiteral("setSetting"), arguments, QStringLiteral("void"));
}
