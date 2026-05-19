TARGET = harbour-stumblefish
TEMPLATE = subdirs

SUBDIRS += daemon
SUBDIRS += src

OTHER_FILES += \
    rpm/harbour-stumblefish.spec \
    rpm/harbour-stumblefish.changes
