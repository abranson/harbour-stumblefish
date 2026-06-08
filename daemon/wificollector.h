// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_WIFICOLLECTOR_H
#define STUMBLEFISH_WIFICOLLECTOR_H

#include <QMetaObject>
#include <QObject>
#include <QTimer>

#include "observations.h"

class NetworkManager;
class NetworkTechnology;

class WifiCollector : public QObject
{
    Q_OBJECT

public:
    explicit WifiCollector(QObject *parent = 0);
    ~WifiCollector();

    void setEnabled(bool enabled);
    void setActiveScanning(bool enabled);
    QList<WifiObservation> observations() const;
    QString status() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void servicesChanged();
    void scanDue();
    void scanFinished();
    void scanTimedOut();

private:
    void scheduleScan(int delayMs = -1);
    void finishScan();
    NetworkTechnology *wifiTechnology() const;

    NetworkManager *m_manager;
    QTimer m_scanTimer;
    QTimer m_scanTimeoutTimer;
    QMetaObject::Connection m_scanFinishedConnection;
    qint64 m_lastScanStartedMs;
    bool m_scanInProgress;
    bool m_activeScanning;
    bool m_enabled;
    QString m_status;
};

#endif
