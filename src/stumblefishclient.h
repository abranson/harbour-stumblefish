#ifndef STUMBLEFISHCLIENT_H
#define STUMBLEFISHCLIENT_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QDBusInterface;
class QDBusPendingCallWatcher;

class StumblefishClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList reports READ reports NOTIFY reportsChanged)
    Q_PROPERTY(QVariantMap selectedReport READ selectedReport NOTIFY selectedReportChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit StumblefishClient(QObject *parent = 0);
    ~StumblefishClient();

    QVariantMap status() const;
    QVariantMap settings() const;
    QVariantList reports() const;
    QVariantMap selectedReport() const;
    bool busy() const;
    QString message() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadReport(int id);
    Q_INVOKABLE void setMode(const QString &mode);
    Q_INVOKABLE void setSourceEnabled(const QString &source, bool enabled);
    Q_INVOKABLE void setEndpoint(const QString &endpoint);
    Q_INVOKABLE void setAutoUploadEnabled(bool enabled);
    Q_INVOKABLE void collectNow();
    Q_INVOKABLE void uploadPending();
    Q_INVOKABLE void retryReport(int id);
    Q_INVOKABLE void deleteReport(int id);
    Q_INVOKABLE void clearPendingReports();

Q_SIGNALS:
    void statusChanged();
    void settingsChanged();
    void reportsChanged();
    void selectedReportChanged();
    void busyChanged();
    void messageChanged();

private Q_SLOTS:
    void handleStatusSignal(const QVariantMap &status);
    void handleSettingsSignal(const QVariantMap &settings);
    void handleReportsSignal();
    void handleUploadFinished(bool success, const QString &message);
    void pendingFinished(QDBusPendingCallWatcher *watcher);

private:
    void asyncCall(const QString &method, const QVariantList &arguments, const QString &kind);
    void setBusyCount(int count);
    void setMessage(const QString &message);
    void setSetting(const QString &key, const QVariant &value);

    QDBusInterface *m_interface;
    QVariantMap m_status;
    QVariantMap m_settings;
    QVariantList m_reports;
    QVariantMap m_selectedReport;
    int m_busyCount;
    QString m_message;
};

#endif
