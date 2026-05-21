# SPDX-License-Identifier: MIT
TARGET = tst_uploaduseragent

CONFIG += console c++11 testcase no_testcase_installs
QT -= gui
QT += core testlib

INCLUDEPATH += ../daemon
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

SOURCES += \
    tst_uploaduseragent.cpp \
    ../daemon/uploaduseragent.cpp

HEADERS += \
    ../daemon/uploaduseragent.h
