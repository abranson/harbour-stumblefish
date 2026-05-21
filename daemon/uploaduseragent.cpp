// SPDX-License-Identifier: MIT
#include "uploaduseragent.h"

#include <QFile>
#include <QString>

namespace {

QString osReleaseValue(const QByteArray &contents, const QByteArray &key)
{
    const QByteArray prefix = key + '=';
    foreach (QByteArray line, contents.split('\n')) {
        line = line.trimmed();
        if (!line.startsWith(prefix)) {
            continue;
        }

        QByteArray value = line.mid(prefix.length()).trimmed();
        if (value.length() >= 2
                && ((value.at(0) == '"' && value.at(value.length() - 1) == '"')
                    || (value.at(0) == '\'' && value.at(value.length() - 1) == '\''))) {
            value = value.mid(1, value.length() - 2);
            value.replace("\\\\", "\\");
            value.replace("\\\"", "\"");
            value.replace("\\'", "'");
        }

        return QString::fromUtf8(value);
    }

    return QString();
}

QString sanitizeUserAgentComment(QString value)
{
    for (int i = 0; i < value.length(); ++i) {
        const ushort character = value.at(i).unicode();
        if (character < 0x20
                || value.at(i) == QLatin1Char('(')
                || value.at(i) == QLatin1Char(')')) {
            value[i] = QLatin1Char(' ');
        }
    }

    return value.simplified();
}

QString osReleaseComment(const QByteArray &contents)
{
    const QString name = sanitizeUserAgentComment(
                osReleaseValue(contents, QByteArrayLiteral("NAME")));
    const QString versionId = sanitizeUserAgentComment(
                osReleaseValue(contents, QByteArrayLiteral("VERSION_ID")));

    if (name.isEmpty() || versionId.isEmpty()) {
        return QString();
    }

    return name + QStringLiteral("; ") + versionId;
}

}

namespace Stumblefish {

QByteArray uploadUserAgent(const QByteArray &osReleaseContents)
{
    QString userAgent = QStringLiteral("harbour-stumblefish/%1")
            .arg(QStringLiteral(APP_VERSION));
    const QString osRelease = osReleaseComment(osReleaseContents);
    if (!osRelease.isEmpty()) {
        userAgent += QStringLiteral(" (") + osRelease + QLatin1Char(')');
    }

    return userAgent.toUtf8();
}

QByteArray uploadUserAgent()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly)) {
        return uploadUserAgent(QByteArray());
    }

    return uploadUserAgent(file.readAll());
}

}
