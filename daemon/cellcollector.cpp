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
const int QOfonoExtCellNrType = 4;

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

int knownCellValue(int value)
{
    return value == QOfonoExtCell::InvalidValue ? -1 : value;
}

int knownCellProperty(const QOfonoExtCell *cell, const char *name)
{
    bool ok = false;
    const int value = cell->property(name).toInt(&ok);
    return ok ? value : QOfonoExtCell::InvalidValue;
}

qint64 knownCellStringProperty(const QOfonoExtCell *cell, const char *name)
{
    bool ok = false;
    const qint64 value = cell->property(name).toString().trimmed().toLongLong(&ok);
    return ok ? value : -1;
}

bool hasCellIdentity(int value)
{
    return value != QOfonoExtCell::InvalidValue && value > 0;
}

bool hasCellIdentity(qint64 value)
{
    return value > 0;
}

bool hasMobileCountryCode(int value)
{
    return value > 0;
}

bool hasMobileNetworkCode(int value)
{
    return value >= 0;
}

bool hasLocationAreaCode(int value)
{
    return value > 0;
}

bool hasPrimaryScramblingCode(int value)
{
    return value >= 0;
}

bool hasAsu(int value)
{
    return value >= 0 && value != 99;
}

bool isCellSignalLevelDbm(int value)
{
    return value != QOfonoExtCell::InvalidValue && value >= -150 && value <= -20;
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
        if (type == QOfonoExtCellNrType) {
            return QStringLiteral("nr");
        }
        return QString();
    }
}

bool hasEnoughCellData(const CellObservation &cell)
{
    if (!hasMobileCountryCode(cell.mobileCountryCode)
            || !hasMobileNetworkCode(cell.mobileNetworkCode)) {
        return false;
    }

    if (cell.radioType == QStringLiteral("gsm")) {
        return hasCellIdentity(cell.cellId)
                || hasLocationAreaCode(cell.locationAreaCode);
    }

    if (cell.radioType == QStringLiteral("wcdma")
            || cell.radioType == QStringLiteral("lte")
            || cell.radioType == QStringLiteral("nr")) {
        return hasCellIdentity(cell.cellId)
                || hasLocationAreaCode(cell.locationAreaCode)
                || hasPrimaryScramblingCode(cell.primaryScramblingCode);
    }

    return false;
}

QString cellKey(const CellObservation &cell)
{
    return cell.radioType + QStringLiteral(":")
            + QString::number(cell.mobileCountryCode) + QStringLiteral(":")
            + QString::number(cell.mobileNetworkCode) + QStringLiteral(":")
            + QString::number(cell.locationAreaCode) + QStringLiteral(":")
            + QString::number(cell.cellId) + QStringLiteral(":")
            + QString::number(cell.primaryScramblingCode) + QStringLiteral(":")
            + QString::number(cell.arfcn);
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
    QList<CellObservation> observations;
    QSet<int> mobileCountryCodes;
    QSet<int> mobileNetworkCodes;
    foreach (const QSharedPointer<QOfonoExtCell> &cell, m_watcher->cells()) {
        const QString type = radioType(cell->type());
        if (type.isEmpty()) {
            continue;
        }

        CellObservation observation;
        observation.radioType = type;
        observation.mobileCountryCode = knownCellValue(cell->mcc());
        observation.mobileNetworkCode = knownCellValue(cell->mnc());
        const int signalLevelDbm = cell->signalLevelDbm();
        observation.signalStrength = isCellSignalLevelDbm(signalLevelDbm) ? signalLevelDbm : 0;
        observation.serving = cell->registered();
        observation.seenMs = now;
        observation.locationAreaCode = -1;
        observation.cellId = -1;
        observation.primaryScramblingCode = -1;
        const int asu = knownCellValue(cell->signalStrength());
        observation.asu = hasAsu(asu) ? asu : -1;
        observation.timingAdvance = -1;
        observation.arfcn = -1;

        if (type == QStringLiteral("lte")) {
            observation.locationAreaCode = knownCellValue(cell->tac());
            if (hasCellIdentity(cell->ci())) {
                observation.cellId = cell->ci();
            }
            observation.primaryScramblingCode = knownCellValue(cell->pci());
            observation.timingAdvance = knownCellValue(cell->timingAdvance());
            observation.arfcn = knownCellValue(cell->earfcn());
        } else if (type == QStringLiteral("nr")) {
            observation.locationAreaCode = knownCellValue(cell->tac());
            observation.cellId = knownCellStringProperty(cell.data(), "nci");
            observation.primaryScramblingCode = knownCellValue(cell->pci());
            observation.arfcn = knownCellValue(knownCellProperty(cell.data(), "nrarfcn"));
        } else {
            observation.locationAreaCode = knownCellValue(cell->lac());
            if (hasCellIdentity(cell->cid())) {
                observation.cellId = cell->cid();
            }
            if (type == QStringLiteral("wcdma")) {
                observation.primaryScramblingCode = knownCellValue(cell->psc());
                observation.arfcn = knownCellValue(cell->uarfcn());
            } else if (type == QStringLiteral("gsm")) {
                observation.arfcn = knownCellValue(cell->arfcn());
            }
        }

        if (hasMobileCountryCode(observation.mobileCountryCode)) {
            mobileCountryCodes.insert(observation.mobileCountryCode);
        }
        if (hasMobileNetworkCode(observation.mobileNetworkCode)) {
            mobileNetworkCodes.insert(observation.mobileNetworkCode);
        }
        observations.append(observation);
    }

    const bool canFillMobileCountryCode = mobileCountryCodes.count() == 1;
    const bool canFillMobileNetworkCode = mobileNetworkCodes.count() == 1;
    const int mobileCountryCode = canFillMobileCountryCode ? *mobileCountryCodes.constBegin() : -1;
    const int mobileNetworkCode = canFillMobileNetworkCode ? *mobileNetworkCodes.constBegin() : -1;
    QSet<QString> seen;
    foreach (CellObservation observation, observations) {
        if (!hasMobileCountryCode(observation.mobileCountryCode) && canFillMobileCountryCode) {
            observation.mobileCountryCode = mobileCountryCode;
        }
        if (!hasMobileNetworkCode(observation.mobileNetworkCode) && canFillMobileNetworkCode) {
            observation.mobileNetworkCode = mobileNetworkCode;
        }
        if (!hasEnoughCellData(observation)) {
            continue;
        }

        const QString key = cellKey(observation);
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
