#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace review {

struct ReviewNote {
    qint64 positionMs = 0;
    QString category;
    QString text;
};

struct ReviewDocument {
    QString mediaPath;
    QList<ReviewNote> notes;
};

QString formatTime(qint64 milliseconds);
bool loadDocument(const QString& path, ReviewDocument& document, QString& error);
bool saveDocument(const QString& path, const ReviewDocument& document, QString& error);
bool exportCsv(const QString& path, const ReviewDocument& document, QString& error);

} // namespace review
