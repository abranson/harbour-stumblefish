// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_WIFICOLLECTOR_H
#define STUMBLEFISH_WIFICOLLECTOR_H

#include <QObject>

#include "observations.h"

class NetworkManager;

class WifiCollector : public QObject
{
    Q_OBJECT

public:
    explicit WifiCollector(QObject *parent = 0);
    ~WifiCollector();

    void setEnabled(bool enabled);
    QList<WifiObservation> observations() const;
    QString status() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void servicesChanged();

private:
    NetworkManager *m_manager;
    bool m_enabled;
    QString m_status;
};

#endif
