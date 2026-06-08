// SPDX-License-Identifier: MIT
#include "wificollector.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QVector>
#include <networkmanager.h>
#include <networkservice.h>
#include <networktechnology.h>

namespace {

const qint64 MaxWifiObservationAgeMs = 30 * 1000;
const qint64 WifiScanIntervalMs = 30 * 1000;
const int WifiScanTimeoutMs = 10 * 1000;

struct IwBss
{
    IwBss()
        : frequency(0)
        , signalStrength(0)
        , lastSeenMs(0)
    {
    }

    QString ssid;
    int frequency;
    int signalStrength;
    qint64 lastSeenMs;
};

int wifiSignalStrengthDbm(uint strength)
{
    if (strength == 0 || strength > 100) {
        return 0;
    }

    // Sailfish ConnMan exposes Wi-Fi RSSI as clamp(120 + rssi, 0, 100).
    return static_cast<int>(strength) - 120;
}

QString iwExecutable()
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("iw"));
    if (!executable.isEmpty()) {
        return executable;
    }
    if (QFileInfo::exists(QStringLiteral("/usr/sbin/iw"))) {
        return QStringLiteral("/usr/sbin/iw");
    }
    if (QFileInfo::exists(QStringLiteral("/sbin/iw"))) {
        return QStringLiteral("/sbin/iw");
    }
    return QString();
}

QString iwOutput(const QStringList &arguments)
{
    const QString executable = iwExecutable();
    if (executable.isEmpty()) {
        return QString();
    }

    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(1000)) {
        return QString();
    }
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return QString();
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QStringList managedWifiInterfaces()
{
    QStringList result;
    QString currentInterface;
    const QStringList lines = iwOutput(QStringList() << QStringLiteral("dev")).split(QLatin1Char('\n'));
    foreach (const QString &line, lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("Interface "))) {
            currentInterface = trimmed.mid(QStringLiteral("Interface ").size()).trimmed();
        } else if (trimmed == QStringLiteral("type managed") && !currentInterface.isEmpty()) {
            result.append(currentInterface);
            currentInterface.clear();
        }
    }
    if (result.isEmpty()) {
        result.append(QStringLiteral("wlan0"));
    }
    return result;
}

QHash<QString, IwBss> parseIwScanDump(const QString &output)
{
    QHash<QString, IwBss> result;
    QRegularExpression bssExpression(QStringLiteral("^BSS\\s+([0-9a-fA-F:]{17})"));
    QString currentBssid;
    IwBss current;

    const QStringList lines = output.split(QLatin1Char('\n'));
    foreach (const QString &line, lines) {
        const QString trimmed = line.trimmed();
        const QRegularExpressionMatch match = bssExpression.match(trimmed);
        if (match.hasMatch()) {
            if (!currentBssid.isEmpty()) {
                result.insert(currentBssid, current);
            }
            currentBssid = match.captured(1).toLower();
            current = IwBss();
            continue;
        }

        if (currentBssid.isEmpty()) {
            continue;
        }

        bool ok = false;
        if (trimmed.startsWith(QStringLiteral("freq:"))) {
            const int frequency = trimmed.mid(5).trimmed().toInt(&ok);
            if (ok && frequency > 0) {
                current.frequency = frequency;
            }
        } else if (trimmed.startsWith(QStringLiteral("signal:"))) {
            const double signal = trimmed.mid(7).trimmed().section(QLatin1Char(' '), 0, 0).toDouble(&ok);
            if (ok && signal < 0.0) {
                current.signalStrength = qRound(signal);
            }
        } else if (trimmed.startsWith(QStringLiteral("last seen:"))) {
            const qint64 lastSeen = trimmed.mid(10).trimmed().section(QLatin1Char(' '), 0, 0).toLongLong(&ok);
            if (ok && lastSeen > 0) {
                current.lastSeenMs = lastSeen;
            }
        } else if (trimmed.startsWith(QStringLiteral("SSID:"))) {
            current.ssid = trimmed.mid(5).trimmed();
        }
    }

    if (!currentBssid.isEmpty()) {
        result.insert(currentBssid, current);
    }
    return result;
}

QHash<QString, IwBss> iwBssByAddress()
{
    QHash<QString, IwBss> result;
    foreach (const QString &interfaceName, managedWifiInterfaces()) {
        const QHash<QString, IwBss> entries = parseIwScanDump(
                    iwOutput(QStringList() << QStringLiteral("dev")
                             << interfaceName << QStringLiteral("scan") << QStringLiteral("dump")));
        QHash<QString, IwBss>::const_iterator it = entries.constBegin();
        for (; it != entries.constEnd(); ++it) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

}

WifiCollector::WifiCollector(QObject *parent)
    : QObject(parent)
    , m_manager(0)
    , m_lastScanStartedMs(0)
    , m_scanInProgress(false)
    , m_activeScanning(false)
    , m_enabled(false)
    , m_status(QStringLiteral("disabled"))
{
    m_scanTimer.setSingleShot(true);
    m_scanTimeoutTimer.setSingleShot(true);
    connect(&m_scanTimer, &QTimer::timeout, this, &WifiCollector::scanDue);
    connect(&m_scanTimeoutTimer, &QTimer::timeout, this, &WifiCollector::scanTimedOut);
}

WifiCollector::~WifiCollector()
{
    setActiveScanning(false);
    delete m_manager;
}

void WifiCollector::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    if (m_enabled) {
        if (!m_manager) {
            m_manager = new NetworkManager(this);
            connect(m_manager, SIGNAL(servicesChanged()), this, SLOT(servicesChanged()));
        }
        m_status = QStringLiteral("enabled");
        if (m_activeScanning) {
            scheduleScan(0);
        }
    } else {
        setActiveScanning(false);
        if (m_manager) {
            m_manager->deleteLater();
            m_manager = 0;
        }
        m_status = QStringLiteral("disabled");
    }
    emit changed();
}

void WifiCollector::setActiveScanning(bool enabled)
{
    if (m_activeScanning == enabled) {
        return;
    }

    m_activeScanning = enabled;
    if (m_activeScanning) {
        scheduleScan(0);
    } else {
        m_scanTimer.stop();
        finishScan();
    }
}

QList<WifiObservation> WifiCollector::observations() const
{
    QList<WifiObservation> result;
    if (!m_enabled || !m_manager) {
        return result;
    }

    NetworkTechnology *technology = wifiTechnology();
    if (!technology || !technology->powered()) {
        return result;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QHash<QString, IwBss> iwEntries = iwBssByAddress();
    QSet<QString> seen;
    QVector<NetworkService *> services = m_manager->getServices(QStringLiteral("wifi"));
    foreach (NetworkService *service, services) {
        if (!service) {
            continue;
        }

        const QString bssid = service->bssid().trimmed().toLower();
        if (bssid.isEmpty() || seen.contains(bssid)) {
            continue;
        }

        const IwBss iw = iwEntries.value(bssid);
        if (iw.lastSeenMs <= 0 || iw.lastSeenMs > MaxWifiObservationAgeMs) {
            continue;
        }

        QString ssid = service->name().trimmed();
        if (ssid.isEmpty() && !iw.ssid.isEmpty()) {
            ssid = iw.ssid.trimmed();
        }
        if (service->hidden()
                || ssid.isEmpty()
                || ssid.endsWith(QStringLiteral("_nomap"), Qt::CaseInsensitive)) {
            continue;
        }

        const int signalStrength = iw.signalStrength < 0
                ? iw.signalStrength
                : wifiSignalStrengthDbm(service->strength());
        const int frequency = iw.frequency > 0 ? iw.frequency : service->frequency();

        WifiObservation observation;
        observation.macAddress = bssid;
        observation.ssid = ssid;
        observation.frequency = frequency;
        observation.signalStrength = signalStrength;
        observation.seenMs = now - iw.lastSeenMs;
        result.append(observation);
        seen.insert(bssid);
    }
    return result;
}

QString WifiCollector::status() const
{
    return m_status;
}

void WifiCollector::servicesChanged()
{
    emit changed();
}

void WifiCollector::scanDue()
{
    if (!m_enabled || !m_activeScanning || !m_manager || m_scanInProgress) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastScanStartedMs > 0 && now - m_lastScanStartedMs < WifiScanIntervalMs) {
        scheduleScan();
        return;
    }

    NetworkTechnology *technology = wifiTechnology();
    if (!technology || !technology->powered()) {
        scheduleScan(WifiScanIntervalMs);
        return;
    }

    m_scanInProgress = true;
    m_lastScanStartedMs = now;
    m_status = QStringLiteral("scanning");
    emit changed();

    m_scanFinishedConnection = connect(technology, &NetworkTechnology::scanFinished,
                                       this, &WifiCollector::scanFinished);
    m_scanTimeoutTimer.start(WifiScanTimeoutMs);
    technology->scan();
}

void WifiCollector::scanFinished()
{
    finishScan();
}

void WifiCollector::scanTimedOut()
{
    finishScan();
}

void WifiCollector::scheduleScan(int delayMs)
{
    if (!m_enabled || !m_activeScanning || !m_manager || m_scanInProgress) {
        return;
    }

    if (delayMs < 0) {
        if (m_lastScanStartedMs <= 0) {
            delayMs = 0;
        } else {
            const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastScanStartedMs;
            delayMs = static_cast<int>(qMax<qint64>(0, WifiScanIntervalMs - elapsed));
        }
    }

    m_scanTimer.start(delayMs);
}

void WifiCollector::finishScan()
{
    QObject::disconnect(m_scanFinishedConnection);
    m_scanFinishedConnection = QMetaObject::Connection();
    m_scanTimeoutTimer.stop();

    if (m_scanInProgress) {
        m_scanInProgress = false;
        m_status = m_enabled ? QStringLiteral("enabled") : QStringLiteral("disabled");
        emit changed();
    }

    scheduleScan();
}

NetworkTechnology *WifiCollector::wifiTechnology() const
{
    return m_manager ? m_manager->getTechnology(QStringLiteral("wifi")) : 0;
}
