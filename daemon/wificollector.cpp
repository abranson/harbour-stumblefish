#include "wificollector.h"

#include <QDateTime>
#include <QSet>
#include <QVector>
#include <networkmanager.h>
#include <networkservice.h>

WifiCollector::WifiCollector(QObject *parent)
    : QObject(parent)
    , m_manager(0)
    , m_enabled(false)
    , m_status(QStringLiteral("disabled"))
{
}

WifiCollector::~WifiCollector()
{
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
    } else {
        if (m_manager) {
            m_manager->deleteLater();
            m_manager = 0;
        }
        m_status = QStringLiteral("disabled");
    }
    emit changed();
}

QList<WifiObservation> WifiCollector::observations() const
{
    QList<WifiObservation> result;
    if (!m_enabled || !m_manager) {
        return result;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> seen;
    QVector<NetworkService *> services = m_manager->getServices(QStringLiteral("wifi"));
    foreach (NetworkService *service, services) {
        if (!service || service->hidden()) {
            continue;
        }

        const QString ssid = service->name().trimmed();
        if (ssid.isEmpty() || ssid.endsWith(QStringLiteral("_nomap"), Qt::CaseInsensitive)) {
            continue;
        }

        const QString bssid = service->bssid().trimmed().toLower();
        if (bssid.isEmpty() || seen.contains(bssid)) {
            continue;
        }

        WifiObservation observation;
        observation.macAddress = bssid;
        observation.frequency = service->frequency();
        observation.signalStrength = service->strength();
        observation.seenMs = now;
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
