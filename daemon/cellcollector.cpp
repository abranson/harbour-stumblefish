// SPDX-License-Identifier: MIT
#include "cellcollector.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QSet>
#include <QSharedPointer>
#include <qofonoextcell.h>
#include <qofonoextcellwatcher.h>

struct OfonoObjectPathProperties
{
    QDBusObjectPath path;
    QVariantMap properties;
};

typedef QList<OfonoObjectPathProperties> OfonoObjectPathPropertiesList;

Q_DECLARE_METATYPE(OfonoObjectPathProperties)
Q_DECLARE_METATYPE(OfonoObjectPathPropertiesList)

QDBusArgument &operator<<(QDBusArgument &argument, const OfonoObjectPathProperties &item)
{
    argument.beginStructure();
    argument << item.path << item.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, OfonoObjectPathProperties &item)
{
    argument.beginStructure();
    argument >> item.path >> item.properties;
    argument.endStructure();
    return argument;
}

namespace {

const char OfonoService[] = "org.ofono";
const char OfonoManagerPath[] = "/";
const char OfonoManagerInterface[] = "org.ofono.Manager";
const char OfonoSimManagerInterface[] = "org.ofono.SimManager";

QVariant unwrap(const QVariant &value)
{
    if (value.canConvert<QDBusVariant>()) {
        return qvariant_cast<QDBusVariant>(value).variant();
    }
    return value;
}

QVariantMap unwrapProperties(const QVariantMap &properties)
{
    QVariantMap result;
    QVariantMap::const_iterator it = properties.constBegin();
    for (; it != properties.constEnd(); ++it) {
        result.insert(it.key(), unwrap(it.value()));
    }
    return result;
}

QStringList stringListProperty(const QVariant &property)
{
    const QVariant value = unwrap(property);
    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }

    QStringList result;
    foreach (const QVariant &item, value.toList()) {
        const QString text = unwrap(item).toString();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}

QString stringProperty(const QVariantMap &properties, const QString &key)
{
    return unwrap(properties.value(key)).toString().trimmed();
}

bool simPresent(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("Present"))) {
        return unwrap(properties.value(QStringLiteral("Present"))).toBool();
    }

    return !stringProperty(properties, QStringLiteral("SubscriberIdentity")).isEmpty()
            || !stringProperty(properties, QStringLiteral("MobileCountryCode")).isEmpty()
            || !stringProperty(properties, QStringLiteral("MobileNetworkCode")).isEmpty();
}

QString radioType(int type)
{
    switch (type) {
    case QOfonoExtCell::GSM:
        return QStringLiteral("gsm");
    case QOfonoExtCell::WCDMA:
        return QStringLiteral("wcdma");
    case QOfonoExtCell::LTE:
        return QStringLiteral("lte");
    default:
        return QString();
    }
}

}

CellCollector::CellCollector(QObject *parent)
    : QObject(parent)
    , m_availabilityTimer(this)
    , m_watcher(0)
    , m_enabled(false)
    , m_available(false)
    , m_unavailableReason(QStringLiteral("No cellular modem detected"))
    , m_status(QStringLiteral("disabled"))
{
    qDBusRegisterMetaType<OfonoObjectPathProperties>();
    qDBusRegisterMetaType<OfonoObjectPathPropertiesList>();

    connect(&m_availabilityTimer, SIGNAL(timeout()), this, SLOT(refreshAvailability()));
    m_availabilityTimer.setInterval(30000);
    m_availabilityTimer.start();
    refreshAvailability();
}

CellCollector::~CellCollector()
{
    delete m_watcher;
}

void CellCollector::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        applyEnabledState(true);
    } else {
        applyEnabledState(false);
    }
}

QList<CellObservation> CellCollector::observations() const
{
    QList<CellObservation> result;
    if (!m_enabled || !m_available || !m_watcher) {
        return result;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> seen;
    foreach (const QSharedPointer<QOfonoExtCell> &cell, m_watcher->cells()) {
        const QString type = radioType(cell->type());
        if (type.isEmpty()) {
            continue;
        }

        CellObservation observation;
        observation.radioType = type;
        observation.mobileCountryCode = cell->mcc();
        observation.mobileNetworkCode = cell->mnc();
        observation.signalStrength = cell->signalStrength();
        observation.serving = false;
        observation.seenMs = now;
        observation.primaryScramblingCode = -1;

        if (cell->cid() != QOfonoExtCell::InvalidValue && cell->cid() != 0) {
            observation.locationAreaCode = cell->lac();
            observation.cellId = cell->cid();
            observation.primaryScramblingCode = cell->psc();
        } else if (cell->ci() != QOfonoExtCell::InvalidValue && cell->ci() != 0) {
            observation.locationAreaCode = cell->tac();
            observation.cellId = cell->ci();
            observation.primaryScramblingCode = cell->pci();
        } else {
            continue;
        }

        if (observation.mobileCountryCode <= 0 || observation.mobileNetworkCode < 0
                || observation.locationAreaCode <= 0 || observation.cellId <= 0) {
            continue;
        }

        const QString key = observation.radioType + QStringLiteral(":")
                + QString::number(observation.mobileCountryCode) + QStringLiteral(":")
                + QString::number(observation.mobileNetworkCode) + QStringLiteral(":")
                + QString::number(observation.locationAreaCode) + QStringLiteral(":")
                + QString::number(observation.cellId);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.append(observation);
    }
    return result;
}

QString CellCollector::status() const
{
    return m_status;
}

bool CellCollector::available() const
{
    return m_available;
}

QString CellCollector::unavailableReason() const
{
    return m_unavailableReason;
}

void CellCollector::refreshAvailability()
{
    QString reason;
    const bool available = detectAvailability(&reason);
    const QString unavailableReason = available ? QString() : reason;
    if (m_available == available && m_unavailableReason == unavailableReason) {
        applyEnabledState(false);
        return;
    }

    m_available = available;
    m_unavailableReason = available
            ? QString()
            : (unavailableReason.isEmpty()
               ? QStringLiteral("Cell collection unavailable")
               : unavailableReason);
    applyEnabledState(true);
}

void CellCollector::applyEnabledState(bool forceSignal)
{
    const QString previousStatus = m_status;

    if (!m_available) {
        if (m_watcher) {
            m_watcher->deleteLater();
            m_watcher = 0;
        }
        m_status = QStringLiteral("unavailable");
    } else if (m_enabled) {
        if (!m_watcher) {
            m_watcher = new QOfonoExtCellWatcher(this);
            connect(m_watcher, SIGNAL(cellsChanged()), this, SLOT(cellsChanged()));
        }
        m_status = QStringLiteral("enabled");
    } else {
        if (m_watcher) {
            m_watcher->deleteLater();
            m_watcher = 0;
        }
        m_status = QStringLiteral("disabled");
    }

    if (forceSignal || previousStatus != m_status) {
        emit changed();
    }
}

bool CellCollector::detectAvailability(QString *reason) const
{
    QDBusInterface manager(QString::fromLatin1(OfonoService),
                           QString::fromLatin1(OfonoManagerPath),
                           QString::fromLatin1(OfonoManagerInterface),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        if (reason) {
            *reason = QStringLiteral("No cellular modem service");
        }
        return false;
    }

    QDBusReply<OfonoObjectPathPropertiesList> reply = manager.call(QStringLiteral("GetModems"));
    if (!reply.isValid()) {
        if (reason) {
            const QString message = reply.error().message();
            *reason = message.isEmpty() ? QStringLiteral("Unable to query cellular modem") : message;
        }
        return false;
    }

    const OfonoObjectPathPropertiesList modems = reply.value();
    if (modems.isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("No cellular modem detected");
        }
        return false;
    }

    bool hasSimManager = false;
    foreach (const OfonoObjectPathProperties &modem, modems) {
        const QVariantMap properties = unwrapProperties(modem.properties);
        const bool interfacesKnown = properties.contains(QStringLiteral("Interfaces"));
        const QStringList interfaces = stringListProperty(properties.value(QStringLiteral("Interfaces")));
        if (interfacesKnown && !interfaces.contains(QString::fromLatin1(OfonoSimManagerInterface))) {
            continue;
        }
        hasSimManager = true;

        QDBusInterface sim(QString::fromLatin1(OfonoService),
                           modem.path.path(),
                           QString::fromLatin1(OfonoSimManagerInterface),
                           QDBusConnection::systemBus());
        QDBusReply<QVariantMap> simReply = sim.call(QStringLiteral("GetProperties"));
        if (!simReply.isValid()) {
            continue;
        }
        if (simPresent(unwrapProperties(simReply.value()))) {
            return true;
        }
    }

    if (reason) {
        *reason = hasSimManager
                ? QStringLiteral("No SIM card inserted")
                : QStringLiteral("Cellular modem has no SIM interface");
    }
    return false;
}

void CellCollector::cellsChanged()
{
    emit changed();
}
