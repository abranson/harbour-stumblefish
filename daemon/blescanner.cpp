// SPDX-License-Identifier: MIT
#include "blescanner.h"

#include <QDateTime>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingReply>
#include <QDebug>
#include <QVariantMap>

typedef QMap<QString, QVariantMap> InterfaceList;
typedef QMap<QDBusObjectPath, InterfaceList> ManagedObjectList;

Q_DECLARE_METATYPE(InterfaceList)
Q_DECLARE_METATYPE(ManagedObjectList)

namespace {

const char BluezService[] = "org.bluez";
const char AdapterPath[] = "/org/bluez/hci0";

QString stringValue(const QVariantMap &map, const QString &key)
{
    return map.value(key).toString();
}

int intValue(const QVariantMap &map, const QString &key, int fallback = 0)
{
    bool ok = false;
    const int value = map.value(key).toInt(&ok);
    return ok ? value : fallback;
}

QStringList stringListValue(const QVariantMap &map, const QString &key)
{
    QStringList result;
    foreach (const QVariant &item, map.value(key).toList()) {
        result.append(item.toString());
    }
    return result;
}

}

BleScanner::BleScanner(QObject *parent)
    : QObject(parent)
    , m_objectManager(new QDBusInterface(QString::fromLatin1(BluezService),
                                         QStringLiteral("/"),
                                         QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                         QDBusConnection::systemBus(),
                                         this))
    , m_enabled(false)
    , m_status(QStringLiteral("disabled"))
{
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();

    m_pollTimer.setInterval(5000);
    connect(&m_pollTimer, SIGNAL(timeout()), this, SLOT(poll()));
}

BleScanner::~BleScanner()
{
    setEnabled(false);
}

void BleScanner::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    if (m_enabled) {
        if (!applyDiscoveryFilter() || !startDiscovery()) {
            clearDiscoveryFilter();
            m_status = QStringLiteral("error");
            emit changed();
            return;
        }
        m_pollTimer.start();
        poll();
        m_status = QStringLiteral("scanning");
    } else {
        m_pollTimer.stop();
        stopDiscovery();
        clearDiscoveryFilter();
        m_observations.clear();
        m_status = QStringLiteral("disabled");
    }
    emit changed();
}

QList<BleObservation> BleScanner::observations() const
{
    QList<BleObservation> result;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    foreach (const BleObservation &observation, m_observations) {
        if (now - observation.seenMs <= 30000) {
            result.append(observation);
        }
    }
    return result;
}

QString BleScanner::status() const
{
    return m_status;
}

void BleScanner::poll()
{
    if (!m_enabled || !m_objectManager || !m_objectManager->isValid()) {
        return;
    }

    QDBusPendingReply<ManagedObjectList> reply = m_objectManager->call(QStringLiteral("GetManagedObjects"));
    reply.waitForFinished();
    if (reply.isError()) {
        m_status = QStringLiteral("error");
        emit changed();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const ManagedObjectList objects = reply.value();
    for (ManagedObjectList::ConstIterator it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const InterfaceList interfaces = it.value();
        if (!interfaces.contains(QStringLiteral("org.bluez.Device1"))) {
            continue;
        }

        const QVariantMap device = interfaces.value(QStringLiteral("org.bluez.Device1"));
        const QString address = stringValue(device, QStringLiteral("Address")).toLower();
        if (address.isEmpty()) {
            continue;
        }

        BleObservation observation;
        observation.macAddress = address;
        observation.addressType = stringValue(device, QStringLiteral("AddressType"));
        observation.name = stringValue(device, QStringLiteral("Name"));
        if (observation.name.isEmpty()) {
            observation.name = stringValue(device, QStringLiteral("Alias"));
        }
        observation.signalStrength = intValue(device, QStringLiteral("RSSI"), 0);
        observation.beaconType = 0;
        observation.uuids = stringListValue(device, QStringLiteral("UUIDs"));
        observation.seenMs = now;
        m_observations.insert(address, observation);
    }

    m_status = QStringLiteral("scanning");
    emit changed();
}

bool BleScanner::startDiscovery()
{
    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           QString::fromLatin1(AdapterPath),
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    const QDBusMessage reply = adapter.call(QStringLiteral("StartDiscovery"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to start BLE discovery:" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::stopDiscovery()
{
    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           QString::fromLatin1(AdapterPath),
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    const QDBusMessage reply = adapter.call(QStringLiteral("StopDiscovery"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to stop BLE discovery:" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::applyDiscoveryFilter()
{
    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           QString::fromLatin1(AdapterPath),
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    filter.insert(QStringLiteral("DuplicateData"), true);
    const QDBusMessage reply = adapter.call(QStringLiteral("SetDiscoveryFilter"), filter);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to set BLE discovery filter:" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::clearDiscoveryFilter()
{
    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           QString::fromLatin1(AdapterPath),
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    const QDBusMessage reply = adapter.call(QStringLiteral("SetDiscoveryFilter"), QVariantMap());
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to clear BLE discovery filter:" << reply.errorMessage();
        return false;
    }
    return true;
}
