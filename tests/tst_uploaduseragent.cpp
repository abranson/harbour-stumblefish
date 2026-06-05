// SPDX-License-Identifier: MIT
#include "uploaduseragent.h"

#include <QtTest>

namespace {

QByteArray expectedUserAgent(const QByteArray &comment = QByteArray())
{
    QByteArray userAgent = QByteArray("harbour-stumblefish/") + APP_VERSION;
    if (!comment.isEmpty()) {
        userAgent += " (" + comment + ')';
    }
    return userAgent;
}

}

class tst_UploadUserAgent : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void appendsOsReleaseComment();
    void acceptsUnquotedOsReleaseValues();
    void omitsCommentWhenOsReleaseIsIncomplete();
    void sanitizesCommentDelimiters();
};

void tst_UploadUserAgent::appendsOsReleaseComment()
{
    const QByteArray osRelease =
            "NAME=\"Sailfish OS\"\n"
            "VERSION_ID=\"5.0.0.68\"\n";

    QCOMPARE(Stumblefish::uploadUserAgent(osRelease),
             expectedUserAgent(QByteArray("Sailfish OS; 5.0.0.68")));
}

void tst_UploadUserAgent::acceptsUnquotedOsReleaseValues()
{
    const QByteArray osRelease =
            "NAME=Sailfish\n"
            "VERSION_ID=5\n";

    QCOMPARE(Stumblefish::uploadUserAgent(osRelease),
             expectedUserAgent(QByteArray("Sailfish; 5")));
}

void tst_UploadUserAgent::omitsCommentWhenOsReleaseIsIncomplete()
{
    QCOMPARE(Stumblefish::uploadUserAgent(QByteArray("NAME=\"Sailfish OS\"\n")),
             expectedUserAgent());
}

void tst_UploadUserAgent::sanitizesCommentDelimiters()
{
    const QByteArray osRelease =
            "NAME=\"Sailfish (OS)\"\n"
            "VERSION_ID=\"5)0\"\n";

    QCOMPARE(Stumblefish::uploadUserAgent(osRelease),
             expectedUserAgent(QByteArray("Sailfish OS; 5 0")));
}

QTEST_APPLESS_MAIN(tst_UploadUserAgent)

#include "tst_uploaduseragent.moc"
