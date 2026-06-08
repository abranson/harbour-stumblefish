// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_BLESCANNER_H
#define STUMBLEFISH_BLESCANNER_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QVariantMap>

#include "observations.h"

class BleScanner;
class QDBusInterface;
class QDBusObjectPath;

typedef QMap<QString, QVariantMap> InterfaceList;

class BluezPropertiesWatcher : public QObject
{
    Q_OBJECT

public:
    BluezPropertiesWatcher(const QString &path, BleScanner *scanner, QObject *parent = 0);
    QString path() const;

private Q_SLOTS:
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated);

private:
    BleScanner *m_scanner;
    QString m_path;
};

class BleScanner : public QObject
{
    Q_OBJECT

public:
    explicit BleScanner(QObject *parent = 0);
    ~BleScanner();

    void setEnabled(bool enabled);
    QList<BleObservation> observations() const;
    QString status() const;
    bool available() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void interfacesAdded(const QDBusObjectPath &path, const InterfaceList &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);

private:
    friend class BluezPropertiesWatcher;

    bool startScanning();
    bool startDiscovery();
    bool stopDiscovery();
    bool applyDiscoveryFilter();
    bool clearDiscoveryFilter();
    bool updateAdapterPath();
    void clearDeviceCache();
    void watchProperties(const QString &path);
    void unwatchProperties(const QString &path);
    void propertiesChanged(const QString &path, const QString &interface,
                           const QVariantMap &changed, const QStringList &invalidated);
    void updateAdapter(const QString &path, const QVariantMap &properties);
    void removeDevice(const QString &path);
    void updateDeviceProperties(const QString &path, const QVariantMap &properties,
                                const QStringList &invalidated, bool fresh);
    void updateDevice(const QString &path, const QVariantMap &properties, qint64 seenMs);

    QDBusInterface *m_objectManager;
    QHash<QString, BleObservation> m_observations;
    QHash<QString, QVariantMap> m_deviceProperties;
    QHash<QString, QString> m_deviceAddresses;
    QHash<QString, BluezPropertiesWatcher *> m_propertiesWatchers;
    QString m_adapterPath;
    bool m_enabled;
    QString m_status;
};

#endif
