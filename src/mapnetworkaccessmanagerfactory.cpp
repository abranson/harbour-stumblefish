#include "mapnetworkaccessmanagerfactory.h"

#include <QDir>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

StumblefishNetworkAccessManager::StumblefishNetworkAccessManager(const QByteArray &userAgent, QObject *parent)
    : QNetworkAccessManager(parent)
    , m_userAgent(userAgent)
{
}

QNetworkReply *StumblefishNetworkAccessManager::createRequest(Operation operation,
                                                              const QNetworkRequest &request,
                                                              QIODevice *outgoingData)
{
    QNetworkRequest adjusted(request);
    if (!m_userAgent.isEmpty() && !adjusted.hasRawHeader("User-Agent")) {
        adjusted.setRawHeader("User-Agent", m_userAgent);
    }
    return QNetworkAccessManager::createRequest(operation, adjusted, outgoingData);
}

StumblefishNetworkAccessManagerFactory::StumblefishNetworkAccessManagerFactory(const QString &userAgent)
    : m_userAgent(userAgent)
{
}

QNetworkAccessManager *StumblefishNetworkAccessManagerFactory::create(QObject *parent)
{
    StumblefishNetworkAccessManager *manager =
            new StumblefishNetworkAccessManager(m_userAgent.toUtf8(), parent);

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString cacheDirectory = cacheRoot + QStringLiteral("/map-tiles");
    QDir().mkpath(cacheDirectory);

    QNetworkDiskCache *cache = new QNetworkDiskCache(manager);
    cache->setCacheDirectory(cacheDirectory);
    manager->setCache(cache);
    return manager;
}
