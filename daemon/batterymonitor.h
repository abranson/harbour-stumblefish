// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_BATTERYMONITOR_H
#define STUMBLEFISH_BATTERYMONITOR_H

#include <QObject>

#include <batterystatus.h>

class BatteryMonitor : public QObject
{
    Q_OBJECT

public:
    explicit BatteryMonitor(QObject *parent = 0);

    bool available() const;
    int chargePercentage() const;
    bool pluggedIn() const;
    bool lowAndUnplugged(int thresholdPercentage) const;

Q_SIGNALS:
    void changed();

private:
    BatteryStatus m_status;
};

#endif
