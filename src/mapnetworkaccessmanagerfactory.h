#ifndef STUMBLEFISH_MAPNETWORKACCESSMANAGERFACTORY_H
#define STUMBLEFISH_MAPNETWORKACCESSMANAGERFACTORY_H

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QQmlNetworkAccessManagerFactory>
#include <QString>

class QNetworkReply;

class StumblefishNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT

public:
    explicit StumblefishNetworkAccessManager(const QByteArray &userAgent, QObject *parent = 0);

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = 0);

private:
    QByteArray m_userAgent;
};

class StumblefishNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    explicit StumblefishNetworkAccessManagerFactory(const QString &userAgent);

    QNetworkAccessManager *create(QObject *parent);

private:
    QString m_userAgent;
};

#endif
