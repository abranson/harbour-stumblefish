// SPDX-License-Identifier: MIT
#include "blescanner.h"

#include <QDateTime>
#include <QByteArray>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QMap>
#include <QVariantMap>

typedef QMap<QDBusObjectPath, InterfaceList> ManagedObjectList;
typedef QMap<quint16, QByteArray> ManufacturerDataMap;
typedef QMap<QString, QByteArray> ServiceDataMap;

Q_DECLARE_METATYPE(InterfaceList)
Q_DECLARE_METATYPE(ManagedObjectList)

namespace {

const char BluezService[] = "org.bluez";
const char AdapterInterface[] = "org.bluez.Adapter1";
const char DeviceInterface[] = "org.bluez.Device1";
const char ObjectManagerInterface[] = "org.freedesktop.DBus.ObjectManager";
const char PropertiesInterface[] = "org.freedesktop.DBus.Properties";

QVariant dbusVariantValue(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>()) {
        return qvariant_cast<QDBusVariant>(value).variant();
    }
    return value;
}

QString stringValue(const QVariantMap &map, const QString &key)
{
    return dbusVariantValue(map.value(key)).toString();
}

bool intValue(const QVariantMap &map, const QString &key, int *value)
{
    bool ok = false;
    const int result = dbusVariantValue(map.value(key)).toInt(&ok);
    if (!ok) {
        return false;
    }

    *value = result;
    return true;
}

QStringList stringListValue(const QVariantMap &map, const QString &key)
{
    QStringList result;
    foreach (const QVariant &item, dbusVariantValue(map.value(key)).toList()) {
        result.append(item.toString());
    }
    return result;
}

QString poweredAdapterPath(const ManagedObjectList &objects)
{
    for (ManagedObjectList::ConstIterator it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const InterfaceList interfaces = it.value();
        if (!interfaces.contains(QString::fromLatin1(AdapterInterface))) {
            continue;
        }

        const QVariantMap adapter = interfaces.value(QString::fromLatin1(AdapterInterface));
        if (dbusVariantValue(adapter.value(QStringLiteral("Powered"))).toBool()) {
            return it.key().path();
        }
    }

    return QString();
}

QByteArray byteArrayValue(const QVariant &value)
{
    const QVariant unwrapped = dbusVariantValue(value);
    if (unwrapped.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(unwrapped);
        QByteArray result;

        argument.beginArray();
        while (!argument.atEnd()) {
            uchar byte = 0;
            argument >> byte;
            result.append(static_cast<char>(byte));
        }
        argument.endArray();

        return result;
    }

    if (unwrapped.type() == QVariant::ByteArray) {
        return unwrapped.toByteArray();
    }

    QByteArray result;
    foreach (const QVariant &item, unwrapped.toList()) {
        bool ok = false;
        const int byte = item.toInt(&ok);
        if (ok) {
            result.append(static_cast<char>(byte & 0xff));
        }
    }
    return result;
}

ManufacturerDataMap manufacturerDataValue(const QVariantMap &map)
{
    ManufacturerDataMap result;
    const QVariant value = dbusVariantValue(map.value(QStringLiteral("ManufacturerData")));
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        return result;
    }

    const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
    argument.beginMap();
    while (!argument.atEnd()) {
        quint16 manufacturer = 0;
        QVariant bytes;

        argument.beginMapEntry();
        argument >> manufacturer >> bytes;
        argument.endMapEntry();

        result.insert(manufacturer, byteArrayValue(bytes));
    }
    argument.endMap();

    return result;
}

ServiceDataMap serviceDataValue(const QVariantMap &map)
{
    ServiceDataMap result;
    const QVariant value = dbusVariantValue(map.value(QStringLiteral("ServiceData")));
    if (value.userType() != qMetaTypeId<QDBusArgument>()) {
        return result;
    }

    const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
    argument.beginMap();
    while (!argument.atEnd()) {
        QString uuid;
        QVariant bytes;

        argument.beginMapEntry();
        argument >> uuid >> bytes;
        argument.endMapEntry();

        result.insert(uuid.toLower(), byteArrayValue(bytes));
    }
    argument.endMap();

    return result;
}

int byteValue(const QByteArray &data, int offset)
{
    return static_cast<unsigned char>(data.at(offset));
}

quint16 bigEndianUint16(const QByteArray &data, int offset)
{
    return (byteValue(data, offset) << 8) | byteValue(data, offset + 1);
}

QString uuidString(const QByteArray &data, int offset)
{
    const QByteArray hex = data.mid(offset, 16).toHex();
    return QString::fromLatin1(hex.left(8))
            + QStringLiteral("-") + QString::fromLatin1(hex.mid(8, 4))
            + QStringLiteral("-") + QString::fromLatin1(hex.mid(12, 4))
            + QStringLiteral("-") + QString::fromLatin1(hex.mid(16, 4))
            + QStringLiteral("-") + QString::fromLatin1(hex.mid(20, 12));
}

QString hexIdentifier(const QByteArray &data, int offset, int length)
{
    return QStringLiteral("0x") + QString::fromLatin1(data.mid(offset, length).toHex());
}

QString manufacturerDataString(const ManufacturerDataMap &data)
{
    QStringList parts;
    for (ManufacturerDataMap::ConstIterator it = data.constBegin(); it != data.constEnd(); ++it) {
        parts.append(QStringLiteral("%1:%2")
                     .arg(it.key(), 4, 16, QLatin1Char('0'))
                     .arg(QString::fromLatin1(it.value().toHex())));
    }
    return parts.join(QStringLiteral(","));
}

QString serviceDataString(const ServiceDataMap &data)
{
    QStringList parts;
    for (ServiceDataMap::ConstIterator it = data.constBegin(); it != data.constEnd(); ++it) {
        parts.append(it.key() + QStringLiteral(":") + QString::fromLatin1(it.value().toHex()));
    }
    return parts.join(QStringLiteral(","));
}

bool isEddystoneUuid(const QString &uuid)
{
    const QString lower = uuid.toLower();
    return lower == QStringLiteral("feaa")
            || lower == QStringLiteral("0000feaa-0000-1000-8000-00805f9b34fb");
}

bool setBeaconIdentifiers(BleObservation *observation, const ManufacturerDataMap &manufacturerData,
                          const ServiceDataMap &serviceData)
{
    for (ManufacturerDataMap::ConstIterator it = manufacturerData.constBegin();
         it != manufacturerData.constEnd(); ++it) {
        const QByteArray data = it.value();

        if (data.size() >= 23 && byteValue(data, 0) == 0x02 && byteValue(data, 1) == 0x15) {
            observation->beaconType = 0x0215;
            observation->id1 = uuidString(data, 2);
            observation->id2 = QString::number(bigEndianUint16(data, 18));
            observation->id3 = QString::number(bigEndianUint16(data, 20));
            return true;
        }

        if (data.size() >= 23 && byteValue(data, 0) == 0xbe && byteValue(data, 1) == 0xac) {
            observation->beaconType = 0xbeac;
            observation->id1 = uuidString(data, 2);
            observation->id2 = QString::number(bigEndianUint16(data, 18));
            observation->id3 = QString::number(bigEndianUint16(data, 20));
            return true;
        }

        if (it.key() == 0x0499 && data.size() >= 24 && byteValue(data, 0) == 0x05) {
            observation->beaconType = 0x990405;
            observation->id1 = hexIdentifier(data, 18, 6);
            return true;
        }
    }

    for (ServiceDataMap::ConstIterator it = serviceData.constBegin(); it != serviceData.constEnd(); ++it) {
        const QByteArray data = it.value();
        if (isEddystoneUuid(it.key()) && data.size() >= 18 && byteValue(data, 0) == 0x00) {
            observation->beaconType = 0;
            observation->id1 = hexIdentifier(data, 2, 10);
            observation->id2 = hexIdentifier(data, 12, 6);
            return true;
        }
    }

    return false;
}

QString objectPathValue(const QVariantMap &map, const QString &key)
{
    const QVariant value = dbusVariantValue(map.value(key));
    if (value.userType() == qMetaTypeId<QDBusObjectPath>()) {
        return qvariant_cast<QDBusObjectPath>(value).path();
    }
    return value.toString();
}

bool isDeviceOnAdapter(const QVariantMap &device, const QString &adapterPath)
{
    if (adapterPath.isEmpty()) {
        return false;
    }

    const QString adapter = objectPathValue(device, QStringLiteral("Adapter"));
    return adapter.isEmpty() || adapter == adapterPath;
}

bool deviceUpdateIsFresh(const QVariantMap &properties)
{
    return properties.contains(QStringLiteral("RSSI"))
            || properties.contains(QStringLiteral("ManufacturerData"))
            || properties.contains(QStringLiteral("ServiceData"))
            || properties.contains(QStringLiteral("UUIDs"));
}

}

BluezPropertiesWatcher::BluezPropertiesWatcher(const QString &path, BleScanner *scanner,
                                               QObject *parent)
    : QObject(parent)
    , m_scanner(scanner)
    , m_path(path)
{
}

QString BluezPropertiesWatcher::path() const
{
    return m_path;
}

void BluezPropertiesWatcher::propertiesChanged(const QString &interface,
                                               const QVariantMap &changed,
                                               const QStringList &invalidated)
{
    if (m_scanner) {
        m_scanner->propertiesChanged(m_path, interface, changed, invalidated);
    }
}

BleScanner::BleScanner(QObject *parent)
    : QObject(parent)
    , m_objectManager(new QDBusInterface(QString::fromLatin1(BluezService),
                                         QStringLiteral("/"),
                                         QString::fromLatin1(ObjectManagerInterface),
                                         QDBusConnection::systemBus(),
                                         this))
    , m_enabled(false)
    , m_status(QStringLiteral("disabled"))
{
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();

    QDBusConnection::systemBus().connect(QString::fromLatin1(BluezService),
                                         QStringLiteral("/"),
                                         QString::fromLatin1(ObjectManagerInterface),
                                         QStringLiteral("InterfacesAdded"),
                                         QStringLiteral("oa{sa{sv}}"),
                                         this,
                                         SLOT(interfacesAdded(QDBusObjectPath,InterfaceList)));
    QDBusConnection::systemBus().connect(QString::fromLatin1(BluezService),
                                         QStringLiteral("/"),
                                         QString::fromLatin1(ObjectManagerInterface),
                                         QStringLiteral("InterfacesRemoved"),
                                         QStringLiteral("oas"),
                                         this,
                                         SLOT(interfacesRemoved(QDBusObjectPath,QStringList)));
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
        startScanning();
    } else {
        stopDiscovery();
        clearDiscoveryFilter();
        clearDeviceCache();
        m_adapterPath.clear();
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

bool BleScanner::available() const
{
    if (!m_objectManager || !m_objectManager->isValid()) {
        return false;
    }

    if (m_enabled) {
        return m_status == QStringLiteral("scanning");
    }

    QDBusPendingReply<ManagedObjectList> reply = m_objectManager->call(QStringLiteral("GetManagedObjects"));
    reply.waitForFinished();
    return !reply.isError() && !poweredAdapterPath(reply.value()).isEmpty();
}

bool BleScanner::startScanning()
{
    if (!m_adapterPath.isEmpty()) {
        stopDiscovery();
        clearDiscoveryFilter();
        m_adapterPath.clear();
    }

    if (!updateAdapterPath()) {
        clearDeviceCache();
        return false;
    }

    if (!applyDiscoveryFilter() || !startDiscovery()) {
        clearDiscoveryFilter();
        m_adapterPath.clear();
        clearDeviceCache();
        m_status = QStringLiteral("error");
        return false;
    }

    m_status = QStringLiteral("scanning");
    return true;
}

void BleScanner::interfacesAdded(const QDBusObjectPath &path, const InterfaceList &interfaces)
{
    const QString objectPath = path.path();
    if (interfaces.contains(QString::fromLatin1(AdapterInterface))) {
        watchProperties(objectPath);
        updateAdapter(objectPath, interfaces.value(QString::fromLatin1(AdapterInterface)));
    }

    if (interfaces.contains(QString::fromLatin1(DeviceInterface))) {
        updateDeviceProperties(objectPath, interfaces.value(QString::fromLatin1(DeviceInterface)),
                               QStringList(), true);
    }
}

void BleScanner::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    const QString objectPath = path.path();
    if (interfaces.contains(QString::fromLatin1(DeviceInterface))) {
        removeDevice(objectPath);
    }
    if (interfaces.contains(QString::fromLatin1(AdapterInterface))) {
        unwatchProperties(objectPath);
        if (objectPath == m_adapterPath) {
            stopDiscovery();
            clearDiscoveryFilter();
            clearDeviceCache();
            m_adapterPath.clear();
            m_status = QStringLiteral("unavailable");
            emit changed();
        }
    }
}

bool BleScanner::startDiscovery()
{
    if (m_adapterPath.isEmpty()) {
        return false;
    }

    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           m_adapterPath,
                           QString::fromLatin1(AdapterInterface),
                           QDBusConnection::systemBus());
    const QDBusMessage reply = adapter.call(QStringLiteral("StartDiscovery"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to start BLE discovery on" << m_adapterPath << ":" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::stopDiscovery()
{
    if (m_adapterPath.isEmpty()) {
        return true;
    }

    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           m_adapterPath,
                           QString::fromLatin1(AdapterInterface),
                           QDBusConnection::systemBus());
    const QDBusMessage reply = adapter.call(QStringLiteral("StopDiscovery"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to stop BLE discovery on" << m_adapterPath << ":" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::applyDiscoveryFilter()
{
    if (m_adapterPath.isEmpty()) {
        return false;
    }

    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           m_adapterPath,
                           QString::fromLatin1(AdapterInterface),
                           QDBusConnection::systemBus());
    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    filter.insert(QStringLiteral("DuplicateData"), true);
    const QDBusMessage reply = adapter.call(QStringLiteral("SetDiscoveryFilter"), filter);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to set BLE discovery filter on" << m_adapterPath << ":" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::clearDiscoveryFilter()
{
    if (m_adapterPath.isEmpty()) {
        return true;
    }

    QDBusInterface adapter(QString::fromLatin1(BluezService),
                           m_adapterPath,
                           QString::fromLatin1(AdapterInterface),
                           QDBusConnection::systemBus());
    const QDBusMessage reply = adapter.call(QStringLiteral("SetDiscoveryFilter"), QVariantMap());
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to clear BLE discovery filter on" << m_adapterPath << ":" << reply.errorMessage();
        return false;
    }
    return true;
}

bool BleScanner::updateAdapterPath()
{
    if (!m_objectManager || !m_objectManager->isValid()) {
        return false;
    }

    QDBusPendingReply<ManagedObjectList> reply = m_objectManager->call(QStringLiteral("GetManagedObjects"));
    reply.waitForFinished();
    if (reply.isError()) {
        qWarning() << "Failed to query BlueZ adapters:" << reply.error().message();
        return false;
    }

    const ManagedObjectList objects = reply.value();
    const QString path = poweredAdapterPath(objects);
    if (path.isEmpty()) {
        m_status = QStringLiteral("unavailable");
        return false;
    }

    m_adapterPath = path;
    for (ManagedObjectList::ConstIterator it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString objectPath = it.key().path();
        const InterfaceList interfaces = it.value();
        if (interfaces.contains(QString::fromLatin1(AdapterInterface))) {
            watchProperties(objectPath);
        }
    }
    for (ManagedObjectList::ConstIterator it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString objectPath = it.key().path();
        const InterfaceList interfaces = it.value();
        if (interfaces.contains(QString::fromLatin1(DeviceInterface))) {
            updateDeviceProperties(objectPath, interfaces.value(QString::fromLatin1(DeviceInterface)),
                                   QStringList(), false);
        }
    }
    return true;
}

void BleScanner::clearDeviceCache()
{
    const QStringList paths = m_propertiesWatchers.keys();
    foreach (const QString &path, paths) {
        unwatchProperties(path);
    }
    m_observations.clear();
    m_deviceProperties.clear();
    m_deviceAddresses.clear();
}

void BleScanner::watchProperties(const QString &path)
{
    if (path.isEmpty() || m_propertiesWatchers.contains(path)) {
        return;
    }

    BluezPropertiesWatcher *watcher = new BluezPropertiesWatcher(path, this, this);
    const bool connected = QDBusConnection::systemBus().connect(
                QString::fromLatin1(BluezService),
                path,
                QString::fromLatin1(PropertiesInterface),
                QStringLiteral("PropertiesChanged"),
                QStringLiteral("sa{sv}as"),
                watcher,
                SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    if (!connected) {
        qWarning() << "Failed to watch BlueZ properties for" << path;
        watcher->deleteLater();
        return;
    }

    m_propertiesWatchers.insert(path, watcher);
}

void BleScanner::unwatchProperties(const QString &path)
{
    BluezPropertiesWatcher *watcher = m_propertiesWatchers.take(path);
    if (!watcher) {
        return;
    }

    QDBusConnection::systemBus().disconnect(QString::fromLatin1(BluezService),
                                            path,
                                            QString::fromLatin1(PropertiesInterface),
                                            QStringLiteral("PropertiesChanged"),
                                            watcher,
                                            SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    watcher->deleteLater();
}

void BleScanner::propertiesChanged(const QString &path, const QString &interface,
                                   const QVariantMap &changed,
                                   const QStringList &invalidated)
{
    if (interface == QString::fromLatin1(AdapterInterface)) {
        updateAdapter(path, changed);
    } else if (interface == QString::fromLatin1(DeviceInterface)) {
        updateDeviceProperties(path, changed, invalidated, deviceUpdateIsFresh(changed));
    }
}

void BleScanner::updateAdapter(const QString &path, const QVariantMap &properties)
{
    if (!properties.contains(QStringLiteral("Powered"))) {
        return;
    }

    const bool powered = dbusVariantValue(properties.value(QStringLiteral("Powered"))).toBool();
    if (!powered && path == m_adapterPath) {
        stopDiscovery();
        clearDiscoveryFilter();
        clearDeviceCache();
        m_adapterPath.clear();
        m_status = QStringLiteral("unavailable");
        emit changed();
    } else if (powered && m_enabled && (m_adapterPath.isEmpty()
                                        || m_status != QStringLiteral("scanning"))) {
        startScanning();
        emit changed();
    }
}

void BleScanner::removeDevice(const QString &path)
{
    const QString address = m_deviceAddresses.take(path);
    if (!address.isEmpty()) {
        m_observations.remove(address);
    }
    m_deviceProperties.remove(path);
    unwatchProperties(path);
    emit changed();
}

void BleScanner::updateDeviceProperties(const QString &path, const QVariantMap &properties,
                                        const QStringList &invalidated, bool fresh)
{
    if (!m_enabled || m_adapterPath.isEmpty()) {
        return;
    }

    QVariantMap merged = m_deviceProperties.value(path);
    foreach (const QString &property, invalidated) {
        merged.remove(property);
    }
    QVariantMap::const_iterator it = properties.constBegin();
    for (; it != properties.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }

    if (!isDeviceOnAdapter(merged, m_adapterPath)) {
        removeDevice(path);
        return;
    }

    watchProperties(path);
    m_deviceProperties.insert(path, merged);
    const QString address = stringValue(merged, QStringLiteral("Address")).toLower();
    if (!address.isEmpty()) {
        m_deviceAddresses.insert(path, address);
    }

    if (fresh) {
        updateDevice(path, merged, QDateTime::currentMSecsSinceEpoch());
        emit changed();
    }
}

void BleScanner::updateDevice(const QString &path, const QVariantMap &properties, qint64 seenMs)
{
    const QString address = stringValue(properties, QStringLiteral("Address")).toLower();
    if (address.isEmpty()) {
        return;
    }

    m_deviceAddresses.insert(path, address);

    int rssi = 0;
    if (!intValue(properties, QStringLiteral("RSSI"), &rssi) || rssi >= 0) {
        m_observations.remove(address);
        return;
    }
    if (dbusVariantValue(properties.value(QStringLiteral("Paired"))).toBool()) {
        m_observations.remove(address);
        return;
    }

    const ManufacturerDataMap manufacturerData = manufacturerDataValue(properties);
    const ServiceDataMap serviceData = serviceDataValue(properties);
    const QStringList uuids = stringListValue(properties, QStringLiteral("UUIDs"));

    BleObservation observation;
    observation.macAddress = address;
    observation.addressType = stringValue(properties, QStringLiteral("AddressType"));
    observation.name = stringValue(properties, QStringLiteral("Name"));
    observation.signalStrength = rssi;
    observation.beaconType = -1;
    observation.uuids = uuids;
    observation.manufacturerData = manufacturerDataString(manufacturerData);
    observation.serviceData = serviceDataString(serviceData);
    if (!setBeaconIdentifiers(&observation, manufacturerData, serviceData)) {
        m_observations.remove(address);
        return;
    }

    observation.seenMs = seenMs;
    m_observations.insert(address, observation);
}
