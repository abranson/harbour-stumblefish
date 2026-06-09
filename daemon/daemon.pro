# SPDX-License-Identifier: MIT
TARGET = harbour-stumblefishd

CONFIG += console c++11 link_pkgconfig
QMAKE_CFLAGS += -fPIE
QMAKE_CXXFLAGS += -fPIE
QMAKE_LFLAGS += -pie

QT -= gui
QT += core dbus network positioning sql

PKGCONFIG += connman-qt5 nemonotifications-qt5 qofonoext systemsettings

INCLUDEPATH += . ../common
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

SOURCES += \
    main.cpp \
    batterymonitor.cpp \
    blescanner.cpp \
    cellcollector.cpp \
    geoclueposition.cpp \
    service.cpp \
    settings.cpp \
    storage.cpp \
    uploaduseragent.cpp \
    uploader.cpp \
    wificollector.cpp

HEADERS += \
    batterymonitor.h \
    blescanner.h \
    cellcollector.h \
    geoclueposition.h \
    observations.h \
    service.h \
    settings.h \
    storage.h \
    uploaduseragent.h \
    uploader.h \
    wificollector.h

INSTALLS += target service dbusservice

target.path = /usr/bin

service.files = harbour-stumblefishd.service
service.path = /usr/lib/systemd/user

dbusservice.files = org.stumblefish.Collector.service
dbusservice.path = /usr/share/dbus-1/services
