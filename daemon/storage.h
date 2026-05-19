#ifndef STUMBLEFISH_STORAGE_H
#define STUMBLEFISH_STORAGE_H

#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>

#include "observations.h"

class Storage : public QObject
{
    Q_OBJECT

public:
    explicit Storage(QObject *parent = 0);
    ~Storage();

    bool open();
    QString lastError() const;

    int addReport(const Report &report);
    QList<Report> recentReports(int limit) const;
    QList<Report> unuploadedReports(int limit) const;
    Report report(int id) const;
    QList<Report> uploadCandidates(int limit) const;
    qint64 lastReportTimestamp() const;
    QVariantMap counts() const;
    bool deleteReport(int id);
    int clearPendingReports();

    bool markUploading(const QList<int> &ids);
    bool markUploaded(const QList<int> &ids);
    bool markFailed(const QList<int> &ids, const QString &error);
    bool markPending(int id);

    static QVariantMap reportSummaryToMap(const Report &report);
    static QVariantMap reportToMap(const Report &report);

Q_SIGNALS:
    void changed();

private:
    bool migrate();
    bool exec(const QString &sql) const;
    QList<WifiObservation> wifiForReport(int reportId) const;
    QList<CellObservation> cellsForReport(int reportId) const;
    QList<BleObservation> bleForReport(int reportId) const;
    bool updateStatus(const QList<int> &ids, const QString &status, const QString &error, bool setUploaded);
    bool deleteReports(const QList<int> &ids);

    QSqlDatabase m_db;
    QString m_lastError;
};

#endif
