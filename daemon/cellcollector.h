#ifndef STUMBLEFISH_CELLCOLLECTOR_H
#define STUMBLEFISH_CELLCOLLECTOR_H

#include <QObject>
#include <QTimer>

#include "observations.h"

class QOfonoExtCellWatcher;

class CellCollector : public QObject
{
    Q_OBJECT

public:
    explicit CellCollector(QObject *parent = 0);
    ~CellCollector();

    void setEnabled(bool enabled);
    QList<CellObservation> observations() const;
    QString status() const;
    bool available() const;
    QString unavailableReason() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void refreshAvailability();
    void cellsChanged();

private:
    void applyEnabledState(bool forceSignal);
    bool detectAvailability(QString *reason) const;

    QTimer m_availabilityTimer;
    QOfonoExtCellWatcher *m_watcher;
    bool m_enabled;
    bool m_available;
    QString m_unavailableReason;
    QString m_status;
};

#endif
