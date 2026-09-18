#pragma once

#include <QString>

bool ContainsSensitiveMediaCredentials(const QString& location);
bool MayPersistMediaLocation(const QString& location);
QString SafeMediaLocationForDisplay(const QString& location);
