// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_GEOCLUEPOSITION_H
#define STUMBLEFISH_GEOCLUEPOSITION_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QGeoCoordinate>
#include <QGeoPositionInfoSource>
#include <QList>
#include <QTimer>

#include "observations.h"

struct GeoclueAccuracy
{
    int level;
    double horizontal;
    double vertical;
};

struct GeoclueSatelliteInfo
{
    int prn;
    int elevation;
    int azimuth;
    int signalStrength;
};

typedef QList<int> GeocluePrnList;
typedef QList<GeoclueSatelliteInfo> GeoclueSatelliteInfoList;

class GeocluePosition : public QObject
{
    Q_OBJECT

public:
    explicit GeocluePosition(QObject *parent = 0);

    void setActive(bool active);
    PositionFix lastFix() const;
    QString status() const;
    bool locationEnabled() const;
    bool hasGnssFix(qint64 timestampMs) const;
    int satellitesInUse() const;

Q_SIGNALS:
    void fixReceived(const PositionFix &fix);
    void statusChanged();

private Q_SLOTS:
    void refreshLocationEnabled();
    void positionUpdated(const QGeoPositionInfo &info);
    void positionError(QGeoPositionInfoSource::Error error);
    void geocluePositionChanged(int fields, int timestamp, double latitude,
                                double longitude, double altitude,
                                const GeoclueAccuracy &accuracy);
    void geoclueSatelliteChanged(int timestamp, int satelliteUsed,
                                 int satelliteVisible, const GeocluePrnList &usedPrn,
                                 const GeoclueSatelliteInfoList &satInfo);
    void expireFix();

private:
    void applyActiveState();
    void emitGnssFix(const PositionFix &fix);
    bool hasFreshFix() const;
    bool shouldAccept(const PositionFix &fix) const;

    QFileSystemWatcher m_locationWatcher;
    QTimer m_fixExpiryTimer;
    QGeoPositionInfoSource *m_source;
    PositionFix m_lastFix;
    QString m_status;
    bool m_active;
    bool m_locationEnabled;
    bool m_updatesStarted;
    int m_satellitesInUse;
    int m_satellitesVisible;
    qint64 m_lastSatelliteMs;
    qint64 m_lastGnssPositionMs;
};

Q_DECLARE_METATYPE(GeoclueAccuracy)
Q_DECLARE_METATYPE(GeocluePrnList)
Q_DECLARE_METATYPE(GeoclueSatelliteInfo)
Q_DECLARE_METATYPE(GeoclueSatelliteInfoList)

#endif
