#ifndef STUMBLEFISH_OBSERVATIONS_H
#define STUMBLEFISH_OBSERVATIONS_H

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct PositionFix
{
    bool valid;
    qint64 timestampMs;
    double latitude;
    double longitude;
    double altitude;
    double accuracy;

    PositionFix()
        : valid(false)
        , timestampMs(0)
        , latitude(0.0)
        , longitude(0.0)
        , altitude(0.0)
        , accuracy(-1.0)
    {
    }
};

struct WifiObservation
{
    QString macAddress;
    int frequency;
    int signalStrength;
    qint64 seenMs;
};

struct CellObservation
{
    QString radioType;
    int mobileCountryCode;
    int mobileNetworkCode;
    int locationAreaCode;
    int cellId;
    int primaryScramblingCode;
    int signalStrength;
    bool serving;
    qint64 seenMs;
};

struct BleObservation
{
    QString macAddress;
    QString addressType;
    QString name;
    int signalStrength;
    int beaconType;
    QString id1;
    QString id2;
    QString id3;
    QStringList uuids;
    QString manufacturerData;
    QString serviceData;
    qint64 seenMs;
};

struct Report
{
    int id;
    qint64 timestampMs;
    PositionFix position;
    QString mode;
    bool wifiEnabled;
    bool cellEnabled;
    bool bleEnabled;
    QString uploadStatus;
    int retryCount;
    QString lastError;
    QString endpoint;
    qint64 uploadedAtMs;
    QList<WifiObservation> wifi;
    QList<CellObservation> cells;
    QList<BleObservation> ble;

    Report()
        : id(0)
        , timestampMs(0)
        , wifiEnabled(false)
        , cellEnabled(false)
        , bleEnabled(false)
        , retryCount(0)
        , uploadedAtMs(0)
    {
    }
};

Q_DECLARE_METATYPE(PositionFix)

#endif
