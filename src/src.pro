TARGET = harbour-stumblefish

CONFIG += c++11 link_pkgconfig
QT += dbus network qml quick
PKGCONFIG += sailfishapp

INCLUDEPATH += . ../common
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

SOURCES += \
    main.cpp \
    mapnetworkaccessmanagerfactory.cpp \
    stumblefishclient.cpp

HEADERS += \
    mapnetworkaccessmanagerfactory.h \
    stumblefishclient.h

DISTFILES += \
    Stumblefish.permission \
    harbour-stumblefish.desktop \
    icons/86x86/apps/harbour-stumblefish.png \
    icons/108x108/apps/harbour-stumblefish.png \
    icons/128x128/apps/harbour-stumblefish.png \
    icons/172x172/apps/harbour-stumblefish.png \
    icons/motorcycle-fish-forward.png \
    icons/motorcycle-fish.png \
    qml/harbour-stumblefish.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AboutPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/MapPage.qml \
    qml/pages/ReportDetailPage.qml \
    qml/pages/ReportsPage.qml \
    qml/pages/SettingsPage.qml

INSTALLS += target qml desktop sailjail_permission about_image

target.path = /usr/bin

qml.files = qml
qml.path = /usr/share/$${TARGET}

desktop.files = $${TARGET}.desktop
desktop.path = /usr/share/applications

sailjail_permission.files = Stumblefish.permission
sailjail_permission.path = /etc/sailjail/permissions

about_image.files = icons/motorcycle-fish.png icons/motorcycle-fish-forward.png
about_image.path = /usr/share/$${TARGET}/icons

QMAKE_RPATHDIR += /usr/share/$${TARGET}/lib
