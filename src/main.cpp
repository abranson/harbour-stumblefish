#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <sailfishapp.h>

#include "constants.h"
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
    view->rootContext()->setContextProperty(QStringLiteral("stumblefish"), &client);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"), application->applicationVersion());
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-stumblefish.qml")));
    view->show();

    return application->exec();
}
