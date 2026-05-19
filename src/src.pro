TARGET = harbour-stumblefish

CONFIG += c++11 link_pkgconfig
QT += dbus qml quick
PKGCONFIG += sailfishapp

INCLUDEPATH += . ../common
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

SOURCES += \
    main.cpp \
    stumblefishclient.cpp

HEADERS += \
    stumblefishclient.h

DISTFILES += \
    Stumblefish.permission \
    harbour-stumblefish.desktop \
    icons/harbour-stumblefish.svg \
    qml/harbour-stumblefish.qml \
    qml/cover/CoverPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/ReportDetailPage.qml \
    qml/pages/ReportsPage.qml \
    qml/pages/SettingsPage.qml

INSTALLS += target qml desktop sailjail_permission

target.path = /usr/bin

qml.files = qml
qml.path = /usr/share/$${TARGET}

desktop.files = $${TARGET}.desktop
desktop.path = /usr/share/applications

sailjail_permission.files = Stumblefish.permission
sailjail_permission.path = /etc/sailjail/permissions

QMAKE_RPATHDIR += /usr/share/$${TARGET}/lib
