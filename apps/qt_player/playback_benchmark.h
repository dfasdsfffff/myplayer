#pragma once

#include <QString>
#include <QStringList>

class QApplication;

struct PlaybackBenchmarkOptions {
    QString mediaPath;
    QString outputPath;
    int runMs = 10000;
    int timeoutMs = 30000;
};

// Only benchmark invocations are parsed; ordinary launch arguments remain unchanged.
bool ParsePlaybackBenchmarkOptions(const QStringList& arguments, PlaybackBenchmarkOptions& options, QString& error);
int RunPlaybackBenchmark(QApplication& application, const PlaybackBenchmarkOptions& options);
