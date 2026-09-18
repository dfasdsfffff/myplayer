#pragma once

#include <QString>

bool IsSupportedMediaLocation(const QString& location);
bool IsSupportedNetworkMediaLocation(const QString& location);
bool IsAudioOnlyMediaLocation(const QString& location);
QString MediaOpenDialogFilter();
QString SubtitleOpenDialogFilter();
