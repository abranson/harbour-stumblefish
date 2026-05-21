// SPDX-License-Identifier: MIT
#include "geoclueposition.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QtGlobal>

namespace {

const char LocationSettingsDir[] = "/var/lib/location";
const char LocationSettingsFile[] = "/var/lib/location/location.conf";
const char LocationEnabledKey[] = "location/enabled";
const qint64 FixFreshMs = 30 * 1000;
const qint64 GnssFixWindowMs = 30 * 1000;

}

QDBusArgument &operator<<(QDBusArgument &argument, const GeoclueAccuracy &accuracy)
{
    argument.beginStructure();
    argument << accuracy.level << accuracy.horizontal << accuracy.vertical;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, GeoclueAccuracy &accuracy)
{
    argument.beginStructure();
    argument >> accuracy.level >> accuracy.horizontal >> accuracy.vertical;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const GeoclueSatelliteInfo &satellite)
{
    argument.beginStructure();
    argument << satellite.prn << satellite.elevation
             << satellite.azimuth << satellite.signalStrength;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, GeoclueSatelliteInfo &satellite)
{
    argument.beginStructure();
    argument >> satellite.prn >> satellite.elevation
             >> satellite.azimuth >> satellite.signalStrength;
    argument.endStructure();
    return argument;
}

GeocluePosition::GeocluePosition(QObject *parent)
    : QObject(parent)
    , m_source(0)
    , m_status(QStringLiteral("idle"))
    , m_active(false)
    , m_locationEnabled(false)
    , m_updatesStarted(false)
    , m_satellitesInUse(0)
    , m_satellitesVisible(0)
    , m_lastSatelliteMs(0)
    , m_lastActiveGnssFixMs(0)
{
    qDBusRegisterMetaType<GeoclueAccuracy>();
    qDBusRegisterMetaType<GeocluePrnList>();
    qDBusRegisterMetaType<GeoclueSatelliteInfo>();
    qDBusRegisterMetaType<GeoclueSatelliteInfoList>();

    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Geoclue.Providers.Hybris"),
                                          QStringLiteral("/org/freedesktop/Geoclue/Providers/Hybris"),
                                          QStringLiteral("org.freedesktop.Geoclue.Position"),
                                          QStringLiteral("PositionChanged"),
                                          this,
                                          SLOT(geocluePositionChanged(int,int,double,double,double,GeoclueAccuracy)));
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Geoclue.Providers.Hybris"),
                                          QStringLiteral("/org/freedesktop/Geoclue/Providers/Hybris"),
                                          QStringLiteral("org.freedesktop.Geoclue.Satellite"),
                                          QStringLiteral("SatelliteChanged"),
                                          this,
                                          SLOT(geoclueSatelliteChanged(int,int,int,GeocluePrnList,GeoclueSatelliteInfoList)));

    m_source = QGeoPositionInfoSource::createDefaultSource(this);
    if (m_source) {
        m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods);
        m_source->setUpdateInterval(3000);
        connect(m_source, SIGNAL(positionUpdated(QGeoPositionInfo)),
                this, SLOT(positionUpdated(QGeoPositionInfo)));
        connect(m_source, SIGNAL(error(QGeoPositionInfoSource::Error)),
                this, SLOT(positionError(QGeoPositionInfoSource::Error)));
    } else {
        m_status = QStringLiteral("no-position-source");
    }

    m_fixExpiryTimer.setSingleShot(true);
    m_fixExpiryTimer.setInterval(FixFreshMs);
    connect(&m_fixExpiryTimer, SIGNAL(timeout()), this, SLOT(expireFix()));

    connect(&m_locationWatcher, SIGNAL(fileChanged(QString)),
            this, SLOT(refreshLocationEnabled()));
    connect(&m_locationWatcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(refreshLocationEnabled()));
    refreshLocationEnabled();
}

void GeocluePosition::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    applyActiveState();
}

PositionFix GeocluePosition::lastFix() const
{
    return hasFreshFix() ? m_lastFix : PositionFix();
}

QString GeocluePosition::status() const
{
    if (m_status == QStringLiteral("available") && !hasFreshFix()) {
        return m_active ? QStringLiteral("acquiring") : QStringLiteral("passive");
    }
    return m_status;
}

bool GeocluePosition::locationEnabled() const
{
    return m_locationEnabled;
}

bool GeocluePosition::hasGnssFix(qint64 timestampMs) const
{
    if (timestampMs <= 0) {
        return false;
    }

    if (m_lastActiveGnssFixMs > 0) {
        const qint64 delta = qAbs(timestampMs - m_lastActiveGnssFixMs);
        if (delta <= GnssFixWindowMs) {
            return true;
        }
    }

    if (m_satellitesInUse <= 0 || m_lastSatelliteMs <= 0) {
        return false;
    }

    return qAbs(timestampMs - m_lastSatelliteMs) <= GnssFixWindowMs;
}

int GeocluePosition::satellitesInUse() const
{
    return m_satellitesInUse;
}

void GeocluePosition::refreshLocationEnabled()
{
    const QFileInfo directory(QString::fromLatin1(LocationSettingsDir));
    if (directory.isDir() && !m_locationWatcher.directories().contains(QString::fromLatin1(LocationSettingsDir))) {
        m_locationWatcher.addPath(QString::fromLatin1(LocationSettingsDir));
    }

    const QFileInfo file(QString::fromLatin1(LocationSettingsFile));
    if (file.exists() && !m_locationWatcher.files().contains(QString::fromLatin1(LocationSettingsFile))) {
        m_locationWatcher.addPath(QString::fromLatin1(LocationSettingsFile));
    }

    QSettings settings(QString::fromLatin1(LocationSettingsFile), QSettings::IniFormat);
    const bool enabled = settings.value(QString::fromLatin1(LocationEnabledKey), false).toBool();
    if (m_locationEnabled == enabled) {
        applyActiveState();
        return;
    }

    m_locationEnabled = enabled;
    applyActiveState();
}

void GeocluePosition::positionUpdated(const QGeoPositionInfo &info)
{
    if (!info.isValid() || !info.coordinate().isValid()) {
        return;
    }

    PositionFix fix;
    fix.valid = true;
    fix.timestampMs = info.timestamp().isValid()
            ? info.timestamp().toMSecsSinceEpoch()
            : QDateTime::currentMSecsSinceEpoch();
    fix.latitude = info.coordinate().latitude();
    fix.longitude = info.coordinate().longitude();
    fix.altitude = info.coordinate().altitude();
    fix.accuracy = info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)
            ? info.attribute(QGeoPositionInfo::HorizontalAccuracy)
            : -1.0;
    m_lastActiveGnssFixMs = fix.timestampMs;
    emitFix(fix);
}

void GeocluePosition::positionError(QGeoPositionInfoSource::Error error)
{
    Q_UNUSED(error);
    m_status = m_locationEnabled ? QStringLiteral("error") : QStringLiteral("location-disabled");
    emit statusChanged();
}

void GeocluePosition::geocluePositionChanged(int fields, int timestamp, double latitude,
                                             double longitude, double altitude,
                                             const GeoclueAccuracy &accuracy)
{
    const bool hasLatitude = (fields & 1) != 0;
    const bool hasLongitude = (fields & 2) != 0;
    if (!hasLatitude || !hasLongitude) {
        return;
    }

    PositionFix fix;
    fix.valid = true;
    fix.timestampMs = static_cast<qint64>(timestamp) * 1000;
    if (fix.timestampMs <= 0) {
        fix.timestampMs = QDateTime::currentMSecsSinceEpoch();
    }
    fix.latitude = latitude;
    fix.longitude = longitude;
    fix.altitude = altitude;
    fix.accuracy = accuracy.horizontal;
    emitFix(fix);
}

void GeocluePosition::geoclueSatelliteChanged(int timestamp, int satelliteUsed,
                                              int satelliteVisible,
                                              const GeocluePrnList &usedPrn,
                                              const GeoclueSatelliteInfoList &satInfo)
{
    Q_UNUSED(usedPrn);
    Q_UNUSED(satInfo);

    m_satellitesInUse = satelliteUsed;
    m_satellitesVisible = satelliteVisible;
    m_lastSatelliteMs = timestamp > 0
            ? static_cast<qint64>(timestamp) * 1000
            : QDateTime::currentMSecsSinceEpoch();
    emit statusChanged();
}

void GeocluePosition::expireFix()
{
    if (!m_lastFix.valid) {
        return;
    }

    m_lastFix = PositionFix();
    applyActiveState();
}

void GeocluePosition::emitFix(const PositionFix &fix)
{
    if (!m_locationEnabled) {
        return;
    }

    if (!shouldAccept(fix)) {
        return;
    }

    m_lastFix = fix;
    m_fixExpiryTimer.start();
    m_status = QStringLiteral("available");
    emit statusChanged();
    emit fixReceived(fix);
}

void GeocluePosition::applyActiveState()
{
    const QString previousStatus = m_status;

    if (!m_source) {
        m_status = QStringLiteral("no-position-source");
    } else if (!m_locationEnabled) {
        if (m_updatesStarted) {
            m_source->stopUpdates();
            m_updatesStarted = false;
        }
        m_fixExpiryTimer.stop();
        m_lastFix = PositionFix();
        m_satellitesInUse = 0;
        m_lastSatelliteMs = 0;
        m_lastActiveGnssFixMs = 0;
        m_status = QStringLiteral("location-disabled");
    } else if (m_active) {
        if (!m_updatesStarted) {
            m_source->startUpdates();
            m_updatesStarted = true;
        }
        m_status = hasFreshFix() ? QStringLiteral("available") : QStringLiteral("acquiring");
    } else {
        if (m_updatesStarted) {
            m_source->stopUpdates();
            m_updatesStarted = false;
        }
        m_status = hasFreshFix() ? QStringLiteral("available") : QStringLiteral("passive");
    }

    if (m_status != previousStatus) {
        emit statusChanged();
    }
}

bool GeocluePosition::hasFreshFix() const
{
    if (!m_locationEnabled || !m_lastFix.valid || m_lastFix.timestampMs <= 0) {
        return false;
    }

    const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_lastFix.timestampMs;
    return age >= -5000 && age <= FixFreshMs;
}

bool GeocluePosition::shouldAccept(const PositionFix &fix) const
{
    if (!fix.valid || fix.accuracy < 0.0 || fix.accuracy > 100.0) {
        return false;
    }

    const qint64 age = QDateTime::currentMSecsSinceEpoch() - fix.timestampMs;
    if (age < -5000 || age > FixFreshMs) {
        return false;
    }

    if (!m_lastFix.valid) {
        return true;
    }

    const QGeoCoordinate previous(m_lastFix.latitude, m_lastFix.longitude);
    const QGeoCoordinate current(fix.latitude, fix.longitude);
    const qreal distance = previous.distanceTo(current);
    const qint64 elapsed = m_lastFix.timestampMs > 0 ? fix.timestampMs - m_lastFix.timestampMs : 0;
    return distance >= 30.0 || elapsed >= 10000;
}
