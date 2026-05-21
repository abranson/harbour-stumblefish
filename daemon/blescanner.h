// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_BLESCANNER_H
#define STUMBLEFISH_BLESCANNER_H

#include <QObject>
#include <QHash>
#include <QTimer>

#include "observations.h"

class QDBusInterface;

class BleScanner : public QObject
{
    Q_OBJECT

public:
    explicit BleScanner(QObject *parent = 0);
    ~BleScanner();

    void setEnabled(bool enabled);
    QList<BleObservation> observations() const;
    QString status() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void poll();

private:
    bool startDiscovery();
    bool stopDiscovery();
    bool applyDiscoveryFilter();
    bool clearDiscoveryFilter();

    QDBusInterface *m_objectManager;
    QTimer m_pollTimer;
    QHash<QString, BleObservation> m_observations;
    bool m_enabled;
    QString m_status;
};

#endif
