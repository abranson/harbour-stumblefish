#include "settings.h"

#include "constants.h"

namespace {
const char ModeKey[] = "collection/mode";
const char WifiKey[] = "sources/wifi";
const char CellKey[] = "sources/cell";
const char BleKey[] = "sources/ble";
const char AutoUploadKey[] = "upload/automatic";
const char UploadOnNonWifiKey[] = "upload/onNonWifi";
const char AllowBackgroundDaemonKey[] = "daemon/allowBackgroundDaemon";
const char RunOnlyWhenAppOpenKey[] = "daemon/runOnlyWhenAppOpen";
const char EndpointKey[] = "upload/endpoint";
const char LastAutoUploadMsKey[] = "upload/lastAutoUploadMs";
const char MapTileUrlTemplateKey[] = "map/tileUrlTemplate";
const char ReportRetentionDaysKey[] = "storage/reportRetentionDays";
const char LastPruneMsKey[] = "storage/lastPruneMs";
const char DefaultMapTileUrlTemplate[] = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";

int normalizedRetentionDays(const QVariant &value)
{
    const int days = value.toInt();
    return days == -1 || days == 30 || days == 60 || days == 180 ? days : 60;
}
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    ensureDefaults();
}

QString Settings::mode() const
{
    const QString current = value(QString::fromLatin1(ModeKey), QStringLiteral("active")).toString();
    return current == QStringLiteral("passive") ? current : QStringLiteral("active");
}

bool Settings::wifiEnabled() const
{
    return value(QString::fromLatin1(WifiKey), true).toBool();
}

bool Settings::cellEnabled() const
{
    return value(QString::fromLatin1(CellKey), true).toBool();
}

bool Settings::bleEnabled() const
{
    return value(QString::fromLatin1(BleKey), false).toBool();
}

bool Settings::autoUploadEnabled() const
{
    return value(QString::fromLatin1(AutoUploadKey), false).toBool();
}

bool Settings::uploadOnNonWifi() const
{
    return value(QString::fromLatin1(UploadOnNonWifiKey), false).toBool();
}

bool Settings::allowBackgroundDaemon() const
{
    return value(QString::fromLatin1(AllowBackgroundDaemonKey), false).toBool();
}

QString Settings::endpoint() const
{
    const QString configured = value(QString::fromLatin1(EndpointKey),
                                     QString::fromLatin1(Stumblefish::DefaultEndpoint)).toString().trimmed();
    return configured.isEmpty() ? QString::fromLatin1(Stumblefish::DefaultEndpoint) : configured;
}

QString Settings::mapTileUrlTemplate() const
{
    return value(QString::fromLatin1(MapTileUrlTemplateKey),
                 QString::fromLatin1(DefaultMapTileUrlTemplate)).toString().trimmed();
}

int Settings::reportRetentionDays() const
{
    return normalizedRetentionDays(value(QString::fromLatin1(ReportRetentionDaysKey), 60));
}

qint64 Settings::lastPruneMs() const
{
    return value(QString::fromLatin1(LastPruneMsKey), 0).toLongLong();
}

void Settings::setLastPruneMs(qint64 timestampMs)
{
    m_settings.setValue(QString::fromLatin1(LastPruneMsKey), timestampMs);
    m_settings.sync();
}

qint64 Settings::lastAutoUploadMs() const
{
    return value(QString::fromLatin1(LastAutoUploadMsKey), 0).toLongLong();
}

void Settings::setLastAutoUploadMs(qint64 timestampMs)
{
    m_settings.setValue(QString::fromLatin1(LastAutoUploadMsKey), timestampMs);
    m_settings.sync();
}

QVariantMap Settings::toMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("mode"), mode());
    map.insert(QStringLiteral("wifiEnabled"), wifiEnabled());
    map.insert(QStringLiteral("cellEnabled"), cellEnabled());
    map.insert(QStringLiteral("bleEnabled"), bleEnabled());
    map.insert(QStringLiteral("autoUploadEnabled"), autoUploadEnabled());
    map.insert(QStringLiteral("uploadOnNonWifi"), uploadOnNonWifi());
    map.insert(QStringLiteral("allowBackgroundDaemon"), allowBackgroundDaemon());
    map.insert(QStringLiteral("endpoint"), endpoint());
    map.insert(QStringLiteral("mapTileUrlTemplate"), mapTileUrlTemplate());
    map.insert(QStringLiteral("reportRetentionDays"), reportRetentionDays());
    return map;
}

void Settings::setValue(const QString &key, const QVariant &newValue)
{
    QString storageKey;
    QVariant value = newValue;

    if (key == QStringLiteral("mode")) {
        storageKey = QString::fromLatin1(ModeKey);
        const QString modeValue = newValue.toString();
        value = modeValue == QStringLiteral("passive") ? QStringLiteral("passive") : QStringLiteral("active");
    } else if (key == QStringLiteral("wifiEnabled")) {
        storageKey = QString::fromLatin1(WifiKey);
        value = newValue.toBool();
    } else if (key == QStringLiteral("cellEnabled")) {
        storageKey = QString::fromLatin1(CellKey);
        value = newValue.toBool();
    } else if (key == QStringLiteral("bleEnabled")) {
        storageKey = QString::fromLatin1(BleKey);
        value = newValue.toBool();
    } else if (key == QStringLiteral("autoUploadEnabled")) {
        storageKey = QString::fromLatin1(AutoUploadKey);
        value = newValue.toBool();
    } else if (key == QStringLiteral("uploadOnNonWifi")) {
        storageKey = QString::fromLatin1(UploadOnNonWifiKey);
        value = newValue.toBool();
    } else if (key == QStringLiteral("allowBackgroundDaemon")) {
        storageKey = QString::fromLatin1(AllowBackgroundDaemonKey);
        value = newValue.toBool();
    } else if (key == QStringLiteral("runOnlyWhenAppOpen")) {
        storageKey = QString::fromLatin1(AllowBackgroundDaemonKey);
        value = !newValue.toBool();
    } else if (key == QStringLiteral("endpoint")) {
        storageKey = QString::fromLatin1(EndpointKey);
        value = newValue.toString().trimmed();
    } else if (key == QStringLiteral("mapTileUrlTemplate")) {
        storageKey = QString::fromLatin1(MapTileUrlTemplateKey);
        value = newValue.toString().trimmed();
    } else if (key == QStringLiteral("reportRetentionDays")) {
        storageKey = QString::fromLatin1(ReportRetentionDaysKey);
        value = normalizedRetentionDays(newValue);
    } else {
        return;
    }

    if (m_settings.value(storageKey) == value) {
        return;
    }

    m_settings.setValue(storageKey, value);
    m_settings.sync();
    emit changed();
}

QVariant Settings::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings.value(key, defaultValue);
}

void Settings::ensureDefaults()
{
    if (!m_settings.contains(QString::fromLatin1(ModeKey))) {
        m_settings.setValue(QString::fromLatin1(ModeKey), QStringLiteral("active"));
    }
    if (!m_settings.contains(QString::fromLatin1(WifiKey))) {
        m_settings.setValue(QString::fromLatin1(WifiKey), true);
    }
    if (!m_settings.contains(QString::fromLatin1(CellKey))) {
        m_settings.setValue(QString::fromLatin1(CellKey), true);
    }
    if (!m_settings.contains(QString::fromLatin1(BleKey))) {
        m_settings.setValue(QString::fromLatin1(BleKey), false);
    }
    if (!m_settings.contains(QString::fromLatin1(AutoUploadKey))) {
        m_settings.setValue(QString::fromLatin1(AutoUploadKey), false);
    }
    if (!m_settings.contains(QString::fromLatin1(UploadOnNonWifiKey))) {
        m_settings.setValue(QString::fromLatin1(UploadOnNonWifiKey), false);
    }
    if (!m_settings.contains(QString::fromLatin1(AllowBackgroundDaemonKey))) {
        const bool allowBackground = m_settings.contains(QString::fromLatin1(RunOnlyWhenAppOpenKey))
                ? !m_settings.value(QString::fromLatin1(RunOnlyWhenAppOpenKey), true).toBool()
                : false;
        m_settings.setValue(QString::fromLatin1(AllowBackgroundDaemonKey), allowBackground);
    }
    if (!m_settings.contains(QString::fromLatin1(EndpointKey))) {
        m_settings.setValue(QString::fromLatin1(EndpointKey), QString::fromLatin1(Stumblefish::DefaultEndpoint));
    }
    if (!m_settings.contains(QString::fromLatin1(LastAutoUploadMsKey))) {
        m_settings.setValue(QString::fromLatin1(LastAutoUploadMsKey), 0);
    }
    if (!m_settings.contains(QString::fromLatin1(MapTileUrlTemplateKey))) {
        m_settings.setValue(QString::fromLatin1(MapTileUrlTemplateKey),
                            QString::fromLatin1(DefaultMapTileUrlTemplate));
    }
    if (!m_settings.contains(QString::fromLatin1(ReportRetentionDaysKey))) {
        m_settings.setValue(QString::fromLatin1(ReportRetentionDaysKey), 60);
    }
    if (!m_settings.contains(QString::fromLatin1(LastPruneMsKey))) {
        m_settings.setValue(QString::fromLatin1(LastPruneMsKey), 0);
    }
    m_settings.sync();
}
