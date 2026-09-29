// SPDX-License-Identifier: MIT
#include "batterymonitor.h"

BatteryMonitor::BatteryMonitor(QObject *parent)
    : QObject(parent)
    , m_status(this)
    , m_displaysettings(this)
{
    connect(&m_status, &BatteryStatus::chargePercentageChanged,
            this, [this](int) { emit changed(); });
    connect(&m_status, &BatteryStatus::chargerStatusChanged,
            this, [this](BatteryStatus::ChargerStatus) { emit changed(); });
    connect(&m_displaysettings, &DisplaySettings::powerSaveModeEnabledChanged,
            this, [this]() { emit changed(); });
}

bool BatteryMonitor::available() const
{
    return m_status.chargePercentage() >= 0;
}

int BatteryMonitor::chargePercentage() const
{
    return m_status.chargePercentage();
}

bool BatteryMonitor::pluggedIn() const
{
    return m_status.chargerStatus() == BatteryStatus::Connected;
}

bool BatteryMonitor::psmEnabled() const
{
    return m_displaysettings.powerSaveModeEnabled();
}

bool BatteryMonitor::lowAndUnplugged(int thresholdPercentage) const
{
    return m_status.chargePercentage() >= 0
            && m_status.chargePercentage() < thresholdPercentage
            && m_status.chargerStatus() == BatteryStatus::Disconnected;
}
