#include "settings.h"

#include "constants.h"

namespace {
const char ModeKey[] = "collection/mode";
const char WifiKey[] = "sources/wifi";
const char CellKey[] = "sources/cell";
const char BleKey[] = "sources/ble";
const char AutoUploadKey[] = "upload/automatic";
const char EndpointKey[] = "upload/endpoint";
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

QString Settings::endpoint() const
{
    const QString configured = value(QString::fromLatin1(EndpointKey),
                                     QString::fromLatin1(Stumblefish::DefaultEndpoint)).toString().trimmed();
    return configured.isEmpty() ? QString::fromLatin1(Stumblefish::DefaultEndpoint) : configured;
}

QVariantMap Settings::toMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("mode"), mode());
    map.insert(QStringLiteral("wifiEnabled"), wifiEnabled());
    map.insert(QStringLiteral("cellEnabled"), cellEnabled());
    map.insert(QStringLiteral("bleEnabled"), bleEnabled());
    map.insert(QStringLiteral("autoUploadEnabled"), autoUploadEnabled());
    map.insert(QStringLiteral("endpoint"), endpoint());
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
    } else if (key == QStringLiteral("endpoint")) {
        storageKey = QString::fromLatin1(EndpointKey);
        value = newValue.toString().trimmed();
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
    if (!m_settings.contains(QString::fromLatin1(EndpointKey))) {
        m_settings.setValue(QString::fromLatin1(EndpointKey), QString::fromLatin1(Stumblefish::DefaultEndpoint));
    }
    m_settings.sync();
}
