#ifndef STUMBLEFISH_SERVICE_H
#define STUMBLEFISH_SERVICE_H

#include <QObject>
#include <QDBusVariant>
#include <QVariantList>
#include <QVariantMap>

#include "blescanner.h"
#include "cellcollector.h"
#include "geoclueposition.h"
#include "settings.h"
#include "storage.h"
#include "uploader.h"
#include "wificollector.h"

class Service : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.stumblefish.Collector")

public:
    explicit Service(QObject *parent = 0);

public Q_SLOTS:
    QVariantMap status() const;
    QVariantMap settings() const;
    QVariantList reports(int limit) const;
    QVariantMap report(int id) const;
    void setSetting(const QString &key, const QDBusVariant &value);
    void collectNow();
    void uploadPending();
    void retryReport(int reportId);
    void deleteReport(int reportId);
    void clearPendingReports();

Q_SIGNALS:
    void statusChanged(const QVariantMap &status);
    void settingsChanged(const QVariantMap &settings);
    void reportsChanged();
    void uploadFinished(bool success, const QString &message);

private Q_SLOTS:
    void applySettings();
    void sourceStateChanged();
    void positionFixReceived(const PositionFix &fix);
    void storageChanged();
    void uploaderFinished(bool success, const QString &message);

private:
    bool anySourceEnabled() const;
    bool collectReport(const PositionFix &fix, const QString &reason);
    bool isDuplicateReport(const Report &report) const;
    void emitStatus();

    Settings m_settings;
    Storage m_storage;
    GeocluePosition m_position;
    WifiCollector m_wifi;
    CellCollector m_cell;
    BleScanner m_ble;
    Uploader m_uploader;
    QString m_lastMessage;
};

#endif
