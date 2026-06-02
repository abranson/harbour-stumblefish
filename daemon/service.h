// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_SERVICE_H
#define STUMBLEFISH_SERVICE_H

#include <QObject>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "blescanner.h"
#include "cellcollector.h"
#include "geoclueposition.h"
#include "settings.h"
#include "storage.h"
#include "uploader.h"
#include "wificollector.h"

class NetworkManager;
class Notification;

class Service : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.stumblefish.Collector")

public:
    explicit Service(QObject *parent = 0);
    ~Service();

public Q_SLOTS:
    QVariantMap status() const;
    QVariantMap settings() const;
    QVariantList reports(int limit) const;
    QVariantMap report(int id) const;
    QVariantMap mapSummary() const;
    QVariantList mapCells(double minLatitude, double minLongitude,
                          double maxLatitude, double maxLongitude, int zoom) const;
    void setSetting(const QString &key, const QDBusVariant &value);
    void collectNow();
    void uploadPending();
    void appOpened();
    void appClosed();
    void turnOffBackgroundActiveMode();
    void retryReport(int reportId);
    void deleteReport(int reportId);
    void clearPendingReports();
    int pruneReports();

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
    void autoUploadDueReports();
    void scheduleAutoUpload();
    void pruneDueReports();
    void emitStatus();
    void clientServiceUnregistered(const QString &serviceName);
    void quitForAppLifecycle();

private:
    bool anySourceEnabled() const;
    bool autoUploadNetworkAllowed(QString *reason = 0) const;
    bool collectionAllowed() const;
    QString effectiveMode() const;
    QString collectionStateMessage() const;
    bool statusNotificationShouldBeVisible() const;
    bool statusNotificationHasTurnOffAction() const;
    bool positionShouldBeActive() const;
    void syncUserServiceEnabled() const;
    void updateStatusNotification(const QString &body);
    void closeStatusNotification();
    void closeStoredStatusNotifications();
    bool collectReport(const PositionFix &fix, const QString &reason);
    bool isDuplicateReport(const Report &report) const;
    void addAppClient(const QString &serviceName);
    void removeAppClient(const QString &serviceName);
    void maybeQuitForAppLifecycle();
    int pruneReportsWithRetention(bool manual);

    Settings m_settings;
    Storage m_storage;
    GeocluePosition m_position;
    WifiCollector m_wifi;
    CellCollector m_cell;
    BleScanner m_ble;
    NetworkManager *m_networkManager;
    Uploader m_uploader;
    QDBusServiceWatcher m_clientWatcher;
    QTimer m_autoUploadTimer;
    QTimer m_pruneTimer;
    QTimer m_lifecycleQuitTimer;
    QSet<QString> m_appClients;
    QString m_lastMessage;
    QString m_lastStatusNotificationBody;
    Notification *m_statusNotification;
    bool m_lastStatusNotificationHasTurnOffAction;
    bool m_statusNotificationVisible;
    bool m_quitWhenIdle;
};

#endif
