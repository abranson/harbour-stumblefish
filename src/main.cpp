#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <sailfishapp.h>

#include "constants.h"
#include "mapnetworkaccessmanagerfactory.h"
#include "stumblefishclient.h"

int main(int argc, char *argv[])
{
    QGuiApplication *application = SailfishApp::application(argc, argv);
    application->setOrganizationName(QString::fromLatin1(Stumblefish::OrganizationName));
    application->setOrganizationDomain(QStringLiteral("stumblefish.org"));
    application->setApplicationName(QString::fromLatin1(Stumblefish::ApplicationName));
    application->setApplicationVersion(QStringLiteral(APP_VERSION));

    StumblefishClient client;

    QQuickView *view = SailfishApp::createView();
    view->engine()->setNetworkAccessManagerFactory(
                new StumblefishNetworkAccessManagerFactory(
                    QStringLiteral("harbour-stumblefish/%1 (+https://github.com/abranson/harbour-stumblefish)")
                    .arg(application->applicationVersion())));
    view->rootContext()->setContextProperty(QStringLiteral("stumblefish"), &client);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), application->applicationVersion());
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-stumblefish.qml")));
    view->show();

    return application->exec();
}
