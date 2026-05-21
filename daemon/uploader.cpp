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
    const qint64 value = reportTimestamp - seenTimestamp;
    if (value < 0) {
        return 0;
    }
    return value > INT_MAX ? INT_MAX : static_cast<int>(value);
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

    m_uploadingIds.clear();
    foreach (const Report &report, reports) {
        m_uploadingIds.append(report.id);
    }
    m_storage->markUploading(m_uploadingIds);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", Stumblefish::uploadUserAgent());

    m_reply = m_network->post(request, buildPayload(reports));
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

QByteArray Uploader::buildPayload(const QList<Report> &reports) const
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
            QJsonObject object;
            object.insert(QStringLiteral("macAddress"), wifi.macAddress);
            if (wifi.frequency > 0) {
                object.insert(QStringLiteral("frequency"), wifi.frequency);
            }
            if (wifi.signalStrength != 0) {
                object.insert(QStringLiteral("signalStrength"), wifi.signalStrength);
            }
            object.insert(QStringLiteral("age"), age(report.timestampMs, wifi.seenMs));
            wifiAccessPoints.append(object);
        }
        if (!wifiAccessPoints.isEmpty()) {
            item.insert(QStringLiteral("wifiAccessPoints"), wifiAccessPoints);
        }

        QJsonArray cellTowers;
        foreach (const CellObservation &cell, report.cells) {
            QJsonObject object;
            object.insert(QStringLiteral("radioType"), cell.radioType);
            object.insert(QStringLiteral("mobileCountryCode"), cell.mobileCountryCode);
            object.insert(QStringLiteral("mobileNetworkCode"), cell.mobileNetworkCode);
            object.insert(QStringLiteral("locationAreaCode"), cell.locationAreaCode);
            object.insert(QStringLiteral("cellId"), cell.cellId);
            if (cell.primaryScramblingCode >= 0) {
                object.insert(QStringLiteral("primaryScramblingCode"), cell.primaryScramblingCode);
            }
            if (cell.signalStrength != 0) {
                object.insert(QStringLiteral("signalStrength"), cell.signalStrength);
            }
            object.insert(QStringLiteral("serving"), cell.serving ? 1 : 0);
            object.insert(QStringLiteral("age"), age(report.timestampMs, cell.seenMs));
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
            if (ble.signalStrength != 0) {
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
            object.insert(QStringLiteral("age"), age(report.timestampMs, ble.seenMs));
            bluetoothBeacons.append(object);
        }
        if (!bluetoothBeacons.isEmpty()) {
            item.insert(QStringLiteral("bluetoothBeacons"), bluetoothBeacons);
        }

        items.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("items"), items);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
