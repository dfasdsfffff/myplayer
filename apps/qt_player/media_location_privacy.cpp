#include "media_location_privacy.h"

#include <QUrl>
#include <QUrlQuery>

namespace {
bool IsSensitiveKey(const QString& key)
{
    const QString normalized = key.toLower();
    return normalized == "token" || normalized == "access_token" || normalized == "auth"
        || normalized == "key" || normalized == "signature" || normalized == "sig";
}
}

bool ContainsSensitiveMediaCredentials(const QString& location)
{
    const QUrl url(location);
    if (!url.isValid() || url.scheme().isEmpty())
        return false;
    if (!url.userName().isEmpty() || !url.password().isEmpty())
        return true;
    const QUrlQuery query(url);
    for (const auto& item : query.queryItems()) {
        if (IsSensitiveKey(item.first))
            return true;
    }
    return false;
}

bool MayPersistMediaLocation(const QString& location)
{
    return !ContainsSensitiveMediaCredentials(location);
}

QString SafeMediaLocationForDisplay(const QString& location)
{
    return ContainsSensitiveMediaCredentials(location) ? QStringLiteral("[受保护的媒体地址]") : location;
}
