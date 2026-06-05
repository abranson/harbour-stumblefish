// SPDX-License-Identifier: MIT
#include "uploader.h"

#include "settings.h"
#include "storage.h"
#include "uploaduseragent.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <climits>

namespace {

const int AutomaticMaxRetries = 5;

int age(qint64 reportTimestamp, qint64 seenTimestamp)
{
    if (reportTimestamp <= 0 || seenTimestamp <= 0) {
        return 0;
    }

    const qint64 value = reportTimestamp - seenTimestamp;
    if (value <= 0) {
        return 0;
    }
    return value > INT_MAX ? INT_MAX : static_cast<int>(value);
}

bool isKnownValue(int value)
{
    return value != INT_MAX && value >= 0;
}

bool isPositiveValue(int value)
{
    return value != INT_MAX && value > 0;
}

bool isUint16(int value)
{
    return isKnownValue(value) && value <= 65535;
}

bool isPositiveUint16(int value)
{
    return isPositiveValue(value) && value <= 65535;
}

void insertPositiveUint16(QJsonObject *object, const QString &key, int value)
{
    if (isPositiveUint16(value)) {
        object->insert(key, value);
    }
}

void insertUint16(QJsonObject *object, const QString &key, int value)
{
    if (isUint16(value)) {
        object->insert(key, value);
    }
}

bool hasEnoughCellData(const CellObservation &cell)
{
    if (cell.radioType.isEmpty()
            || !isPositiveUint16(cell.mobileCountryCode)
            || !isUint16(cell.mobileNetworkCode)) {
        return false;
    }

    if (cell.radioType == QStringLiteral("gsm")) {
        return isPositiveValue(cell.cellId)
                || isPositiveUint16(cell.locationAreaCode);
    }

    if (cell.radioType == QStringLiteral("wcdma")
            || cell.radioType == QStringLiteral("lte")
            || cell.radioType == QStringLiteral("nr")) {
        return isPositiveValue(cell.cellId)
                || isPositiveUint16(cell.locationAreaCode)
                || isUint16(cell.primaryScramblingCode);
    }

    return false;
}

}

Uploader::Uploader(Storage *storage, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
    , m_reply(0)
{
}

bool Uploader::uploading() const
{
    return m_reply != 0;
}

void Uploader::uploadPending()
{
    uploadPending(-1);
}

void Uploader::uploadAutomatically()
{
    uploadPending(AutomaticMaxRetries);
}

void Uploader::uploadPending(int maxRetryCount)
{
    if (m_reply) {
        emit uploadFinished(false, QStringLiteral("Upload already in progress"));
        return;
    }

    const QList<Report> reports = m_storage->uploadCandidates(950, maxRetryCount);
    if (reports.isEmpty()) {
        emit uploadFinished(true, QStringLiteral("No pending reports"));
        return;
    }

    QUrl url(m_settings->endpoint());
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        emit uploadFinished(false, QStringLiteral("Upload endpoint is invalid"));
        return;
    }

    QList<int> includedIds;
    const QByteArray payload = buildPayload(reports, &includedIds);
    QList<int> skippedIds;
    foreach (const Report &report, reports) {
        if (!includedIds.contains(report.id)) {
            skippedIds.append(report.id);
        }
    }
    if (!skippedIds.isEmpty()) {
        m_storage->markFailed(skippedIds, QStringLiteral("Report has no uploadable radio observations"));
    }
    if (includedIds.isEmpty()) {
        emit uploadFinished(false, QStringLiteral("No uploadable reports"));
        return;
    }

    m_uploadingIds = includedIds;
    m_storage->markUploading(m_uploadingIds);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", Stumblefish::uploadUserAgent());

    m_reply = m_network->post(request, payload);
    connect(m_reply, SIGNAL(finished()), this, SLOT(replyFinished()));
}

void Uploader::retryReport(int reportId)
{
    m_storage->markPending(reportId);
    uploadPending();
}

void Uploader::replyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = 0;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    QString message;
    bool success = false;

    if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
        success = true;
        message = QStringLiteral("Uploaded %1 reports").arg(m_uploadingIds.count());
        m_storage->markUploaded(m_uploadingIds);
    } else {
        message = reply->errorString();
        if (!body.isEmpty()) {
            message += QStringLiteral(": ") + QString::fromUtf8(body.left(300));
        }
        if (message.trimmed().isEmpty()) {
            message = QStringLiteral("HTTP %1").arg(status);
        }
        m_storage->markFailed(m_uploadingIds, message);
    }

    reply->deleteLater();
    m_uploadingIds.clear();
    emit uploadFinished(success, message);
}

QByteArray Uploader::buildPayload(const QList<Report> &reports, QList<int> *includedIds) const
{
    QJsonArray items;
    foreach (const Report &report, reports) {
        QJsonObject item;
        item.insert(QStringLiteral("timestamp"), static_cast<double>(report.timestampMs));

        QJsonObject position;
        position.insert(QStringLiteral("latitude"), report.position.latitude);
        position.insert(QStringLiteral("longitude"), report.position.longitude);
        if (report.position.altitude == report.position.altitude) {
            position.insert(QStringLiteral("altitude"), report.position.altitude);
        }
        position.insert(QStringLiteral("accuracy"), report.position.accuracy);
        item.insert(QStringLiteral("position"), position);

        QJsonArray wifiAccessPoints;
        foreach (const WifiObservation &wifi, report.wifi) {
            if (wifi.macAddress.isEmpty() || wifi.ssid.isEmpty()) {
                continue;
            }

            QJsonObject object;
            object.insert(QStringLiteral("macAddress"), wifi.macAddress);
            object.insert(QStringLiteral("ssid"), wifi.ssid);
            if (wifi.frequency > 0) {
                object.insert(QStringLiteral("frequency"), wifi.frequency);
            }
            if (wifi.signalStrength < 0) {
                object.insert(QStringLiteral("signalStrength"), wifi.signalStrength);
            }
            const int observationAge = age(report.timestampMs, wifi.seenMs);
            if (observationAge > 0) {
                object.insert(QStringLiteral("age"), observationAge);
            }
            wifiAccessPoints.append(object);
        }
        if (!wifiAccessPoints.isEmpty()) {
            item.insert(QStringLiteral("wifiAccessPoints"), wifiAccessPoints);
        }

        QJsonArray cellTowers;
        foreach (const CellObservation &cell, report.cells) {
            if (!hasEnoughCellData(cell)) {
                continue;
            }

            QJsonObject object;
            object.insert(QStringLiteral("radioType"), cell.radioType);
            insertPositiveUint16(&object, QStringLiteral("mobileCountryCode"),
                                 cell.mobileCountryCode);
            insertUint16(&object, QStringLiteral("mobileNetworkCode"),
                         cell.mobileNetworkCode);
            insertPositiveUint16(&object, QStringLiteral("locationAreaCode"),
                                 cell.locationAreaCode);
            if (isPositiveValue(cell.cellId)) {
                object.insert(QStringLiteral("cellId"), cell.cellId);
            }
            insertUint16(&object, QStringLiteral("primaryScramblingCode"),
                         cell.primaryScramblingCode);
            if (cell.signalStrength < 0) {
                object.insert(QStringLiteral("signalStrength"), cell.signalStrength);
            }
            object.insert(QStringLiteral("serving"), cell.serving ? 1 : 0);
            cellTowers.append(object);
        }
        if (!cellTowers.isEmpty()) {
            item.insert(QStringLiteral("cellTowers"), cellTowers);
        }

        QJsonArray bluetoothBeacons;
        foreach (const BleObservation &ble, report.ble) {
            QJsonObject object;
            object.insert(QStringLiteral("macAddress"), ble.macAddress);
            if (!ble.name.isEmpty()) {
                object.insert(QStringLiteral("name"), ble.name);
            }
            if (ble.signalStrength < 0) {
                object.insert(QStringLiteral("signalStrength"), ble.signalStrength);
            }
            if (ble.beaconType != 0) {
                object.insert(QStringLiteral("beaconType"), ble.beaconType);
            }
            if (!ble.id1.isEmpty()) {
                object.insert(QStringLiteral("id1"), ble.id1);
            }
            if (!ble.id2.isEmpty()) {
                object.insert(QStringLiteral("id2"), ble.id2);
            }
            if (!ble.id3.isEmpty()) {
                object.insert(QStringLiteral("id3"), ble.id3);
            }
            bluetoothBeacons.append(object);
        }
        if (!bluetoothBeacons.isEmpty()) {
            item.insert(QStringLiteral("bluetoothBeacons"), bluetoothBeacons);
        }

        if (wifiAccessPoints.count() < 2 && cellTowers.isEmpty() && bluetoothBeacons.isEmpty()) {
            continue;
        }

        if (includedIds) {
            includedIds->append(report.id);
        }
        items.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("items"), items);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
