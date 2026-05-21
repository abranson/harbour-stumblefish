# SPDX-License-Identifier: MIT
TARGET = harbour-stumblefish
TEMPLATE = subdirs

SUBDIRS += daemon
SUBDIRS += src
SUBDIRS += tests

OTHER_FILES += \
    rpm/harbour-stumblefish.spec \
    rpm/harbour-stumblefish.changes
