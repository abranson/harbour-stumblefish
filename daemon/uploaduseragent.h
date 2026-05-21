// SPDX-License-Identifier: MIT
#ifndef STUMBLEFISH_UPLOADUSERAGENT_H
#define STUMBLEFISH_UPLOADUSERAGENT_H

#include <QByteArray>

namespace Stumblefish {

QByteArray uploadUserAgent();
QByteArray uploadUserAgent(const QByteArray &osReleaseContents);

}

#endif
