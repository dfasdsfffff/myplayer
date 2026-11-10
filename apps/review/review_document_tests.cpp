#include "review_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStringList>

#include <iostream>
#include <limits>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool equal(const review::ReviewDocument& left, const review::ReviewDocument& right)
{
    if (left.mediaPath != right.mediaPath || left.notes.size() != right.notes.size())
        return false;
    for (qsizetype index = 0; index < left.notes.size(); ++index) {
        const auto& a = left.notes.at(index);
        const auto& b = right.notes.at(index);
        if (a.positionMs != b.positionMs || a.category != b.category || a.text != b.text)
            return false;
    }
    return true;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject validJson()
{
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("myplayer-review")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("media"), QStringLiteral("../媒体/不存在的视频.mp4")},
        {QStringLiteral("notes"), QJsonArray{QJsonObject{
            {QStringLiteral("positionMs"), 1234},
            {QStringLiteral("category"), QStringLiteral("问题")},
            {QStringLiteral("text"), QStringLiteral("保留标注")}}}}};
}

bool rejectsWithoutMutation(const QString& path, const QByteArray& bytes,
                            const review::ReviewDocument& original)
{
    review::ReviewDocument actual = original;
    QString error;
    return writeFile(path, bytes) && !review::loadDocument(path, actual, error)
        && !error.isEmpty() && equal(actual, original);
}

// Parse exported CSV records, including embedded newlines, rather than comparing a snapshot.
bool parseCsv(const QByteArray& bytes, QList<QStringList>& records)
{
    if (!bytes.startsWith(QByteArray("\xEF\xBB\xBF")))
        return false;
    const QString text = QString::fromUtf8(bytes.sliced(3));
    QStringList record;
    QString field;
    bool quoted = false;
    bool closed = false;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('"')) {
                    field += ch;
                    ++index;
                } else {
                    quoted = false;
                    closed = true;
                }
            } else {
                field += ch;
            }
        } else if (ch == QLatin1Char(',') || ch == QLatin1Char('\r')) {
            record.append(field);
            field.clear();
            closed = false;
            if (ch == QLatin1Char('\r')) {
                if (++index >= text.size() || text.at(index) != QLatin1Char('\n'))
                    return false;
                records.append(record);
                record.clear();
            }
        } else if (ch == QLatin1Char('"') && field.isEmpty() && !closed) {
            quoted = true;
        } else {
            if (closed || ch == QLatin1Char('\n') || ch == QLatin1Char('"'))
                return false;
            field += ch;
        }
    }
    return !quoted && field.isEmpty() && record.isEmpty();
}

bool roundTripAndCsv(const QDir& directory)
{
    if (!Expect(directory.mkpath(QStringLiteral("first"))
                    && directory.mkpath(QStringLiteral("second/deep")),
                "project directories are created"))
        return false;
    review::ReviewDocument document;
    document.mediaPath = directory.filePath(QStringLiteral("媒体/未下载,\"视频\".mp4"));
    document.notes = {
        {2000, QStringLiteral("问题"), QStringLiteral("中文,\"引号\"\n下一行\r\n末行")},
        {2000, QStringLiteral("建议"), QStringLiteral("同一毫秒的第二条")},
        {0, QStringLiteral("重点"), QStringLiteral("=SUM(1,2)")},
        {9007199254740991LL, QStringLiteral("其他"), QStringLiteral("最大精确时间")}};
    const QString first = directory.filePath(QStringLiteral("first/a.review.json"));
    const QString second = directory.filePath(QStringLiteral("second/deep/b.review.json"));
    QString error = QStringLiteral("stale error");
    if (!Expect(review::saveDocument(first, document, error) && error.isEmpty(),
                "save succeeds and clears the previous error"))
        return false;
    const QJsonObject firstJson = QJsonDocument::fromJson(readFile(first)).object();
    const QString storedMedia = firstJson.value(QStringLiteral("media")).toString();
    if (!Expect(QDir::isRelativePath(storedMedia)
                    && QDir::cleanPath(QFileInfo(first).absoluteDir().absoluteFilePath(storedMedia))
                        == document.mediaPath,
                "media is stored relative to the project directory"))
        return false;
    review::ReviewDocument loaded;
    if (!Expect(review::loadDocument(first, loaded, error) && error.isEmpty()
                    && equal(loaded, document),
                "missing media, Chinese, newlines, exact times and note order round trip"))
        return false;
    if (!Expect(review::saveDocument(second, loaded, error), "save as in another directory succeeds"))
        return false;
    review::ReviewDocument moved;
    if (!Expect(review::loadDocument(second, moved, error) && equal(moved, document),
                "cross-directory save as preserves the absolute media identity"))
        return false;
    const QString secondMedia = QJsonDocument::fromJson(readFile(second)).object()
                                    .value(QStringLiteral("media")).toString();
    if (!Expect(QDir::isRelativePath(secondMedia) && secondMedia != storedMedia,
                "save as rebases the relative media path"))
        return false;

    const QStringList formulas{QStringLiteral("=1+1"), QStringLiteral("+2"), QStringLiteral("-3"),
                               QStringLiteral("@command"), QStringLiteral("\tvalue"),
                               QStringLiteral("\rvalue"), QStringLiteral("  =1")};
    review::ReviewDocument exported = document;
    for (const QString& formula : formulas)
        exported.notes.append({42, QStringLiteral("其他"), formula});
    const QString csvPath = directory.filePath(QStringLiteral("notes.csv"));
    if (!Expect(review::exportCsv(csvPath, exported, error) && error.isEmpty(),
                "CSV export succeeds"))
        return false;
    QList<QStringList> rows;
    if (!Expect(parseCsv(readFile(csvPath), rows) && rows.size() == exported.notes.size() + 1,
                "CSV has a UTF-8 BOM, CRLF records and valid escaping"))
        return false;
    if (!Expect(rows.first() == QStringList{QStringLiteral("media"), QStringLiteral("time_ms"),
                                           QStringLiteral("timecode"), QStringLiteral("category"),
                                           QStringLiteral("text")},
                "CSV has the documented semantic columns"))
        return false;
    for (qsizetype index = 0; index < exported.notes.size(); ++index) {
        const auto& note = exported.notes.at(index);
        const auto& row = rows.at(index + 1);
        const bool protectedText = index == 2 || index >= document.notes.size();
        if (!Expect(row.size() == 5 && row.at(0) == document.mediaPath
                        && row.at(1).toLongLong() == note.positionMs
                        && row.at(2) == review::formatTime(note.positionMs)
                        && row.at(3) == note.category
                        && row.at(4) == (protectedText ? QStringLiteral("'") + note.text : note.text),
                    "CSV fields preserve content and neutralize spreadsheet formulas"))
            return false;
    }
    return true;
}

bool validationAndFailure(const QDir& directory)
{
    const QString path = directory.filePath(QStringLiteral("invalid.review.json"));
    review::ReviewDocument original{directory.filePath(QStringLiteral("original.mp4")),
                                   {{7, QStringLiteral("其他"), QStringLiteral("原始数据")}}};
    if (!Expect(rejectsWithoutMutation(path, QByteArray("{"), original)
                    && rejectsWithoutMutation(path, QByteArray("[]"), original),
                "malformed JSON and non-object roots leave the destination unchanged"))
        return false;
    QList<QJsonObject> badRoots;
    auto root = validJson();
    root.insert(QStringLiteral("format"), QStringLiteral("other"));
    badRoots.append(root);
    for (const QJsonValue& version : {QJsonValue(2), QJsonValue(true), QJsonValue(QStringLiteral("1")),
                                      QJsonValue(QJsonValue::Null)}) {
        root = validJson();
        root.insert(QStringLiteral("version"), version);
        badRoots.append(root);
    }
    for (const QJsonValue& media : {QJsonValue(12), QJsonValue(QString()),
                                    QJsonValue(QString(QChar::Null))}) {
        root = validJson();
        root.insert(QStringLiteral("media"), media);
        badRoots.append(root);
    }
    root = validJson();
    root.insert(QStringLiteral("notes"), QJsonObject());
    badRoots.append(root);
    root = validJson();
    root.remove(QStringLiteral("media"));
    badRoots.append(root);
    for (const auto& bad : badRoots) {
        if (!Expect(rejectsWithoutMutation(path, QJsonDocument(bad).toJson(), original),
                    "invalid format, version and root field types are rejected transactionally"))
            return false;
    }
    const QList<QPair<QString, QJsonValue>> badFields{
        {QStringLiteral("positionMs"), -1}, {QStringLiteral("positionMs"), 1.5},
        {QStringLiteral("positionMs"), 9007199254740992.0},
        {QStringLiteral("positionMs"), QStringLiteral("1")},
        {QStringLiteral("positionMs"), true}, {QStringLiteral("positionMs"), QJsonValue()},
        {QStringLiteral("category"), QStringLiteral("未定义")}, {QStringLiteral("category"), 1},
        {QStringLiteral("text"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("text"), QStringLiteral(" \r\n\t")},
        {QStringLiteral("text"), QString(10001, QLatin1Char('x'))}};
    for (const auto& field : badFields) {
        root = validJson();
        QJsonObject note = root.value(QStringLiteral("notes")).toArray().first().toObject();
        note.insert(field.first, field.second);
        // A valid prefix ensures rejection cannot partially install notes.
        QJsonArray notes = root.value(QStringLiteral("notes")).toArray();
        notes.append(note);
        root.insert(QStringLiteral("notes"), notes);
        if (!Expect(rejectsWithoutMutation(path, QJsonDocument(root).toJson(), original),
                    "invalid note fields reject the entire document"))
            return false;
    }
    root = validJson();
    root.insert(QStringLiteral("notes"), QJsonArray{false});
    if (!Expect(rejectsWithoutMutation(path, QJsonDocument(root).toJson(), original),
                "non-object notes are rejected"))
        return false;

    const QByteArray sentinel("existing destination contents");
    if (!Expect(writeFile(path, sentinel) && writeFile(original.mediaPath, sentinel),
                "existing destinations are created"))
        return false;
    review::ReviewDocument invalid = original;
    invalid.notes.first().positionMs = -1;
    QString error;
    if (!Expect(!review::saveDocument(path, invalid, error) && !error.isEmpty()
                    && readFile(path) == sentinel,
                "invalid save preserves the previously existing destination"))
        return false;
    if (!Expect(!review::exportCsv(path, invalid, error) && !error.isEmpty()
                    && readFile(path) == sentinel,
                "invalid CSV export preserves the previously existing destination"))
        return false;
    if (!Expect(!review::saveDocument(original.mediaPath, original, error) && !error.isEmpty()
                    && !review::exportCsv(original.mediaPath, original, error) && !error.isEmpty()
                    && readFile(original.mediaPath) == sentinel,
                "project save and CSV export cannot overwrite the media"))
        return false;
    const QString alias = directory.filePath(QStringLiteral("sub/../original.mp4"));
    if (!Expect(directory.mkpath(QStringLiteral("sub"))
                    && !review::saveDocument(alias, original, error)
                    && !review::exportCsv(alias, original, error)
                    && readFile(original.mediaPath) == sentinel,
                "normalized media aliases cannot bypass overwrite protection"))
        return false;
    const review::ReviewDocument beforeFailure = original;
    const QString unavailable = directory.filePath(QStringLiteral("not-created/out.json"));
    if (!Expect(!review::saveDocument(unavailable, original, error) && !error.isEmpty()
                    && !review::exportCsv(unavailable, original, error) && !error.isEmpty()
                    && equal(original, beforeFailure) && readFile(path) == sentinel,
                "I/O failures preserve the source document and existing files"))
        return false;
    review::ReviewDocument unchanged = original;
    if (!Expect(!review::loadDocument(unavailable, unchanged, error) && !error.isEmpty()
                    && equal(unchanged, original),
                "load open failure preserves the destination document"))
        return false;
    invalid = original;
    invalid.mediaPath = QStringLiteral("relative.mp4");
    return Expect(!review::saveDocument(path, invalid, error) && !error.isEmpty()
                      && readFile(path) == sentinel,
                  "in-memory media paths must be absolute");
}

bool boundaries(const QDir& directory)
{
    QString error;
    const QString path = directory.filePath(QStringLiteral("limits.review.json"));
    review::ReviewDocument document{directory.filePath(QStringLiteral("missing.mp4")), {}};
    const QString emoji = QString::fromUcs4(U"😀");
    const QString text = emoji.repeated(5000);
    document.notes.append({9007199254740991LL, QStringLiteral("重点"), text});
    review::ReviewDocument loaded;
    if (!Expect(text.size() == 10000 && review::saveDocument(path, document, error)
                    && review::loadDocument(path, loaded, error) && equal(loaded, document),
                "10000 UTF-16 units and the largest exact JSON timestamp are accepted"))
        return false;
    const QByteArray saved = readFile(path);
    document.notes.first().text += QLatin1Char('x');
    if (!Expect(!review::saveDocument(path, document, error) && !error.isEmpty()
                    && readFile(path) == saved,
                "one extra UTF-16 unit rejects save without replacing the existing file"))
        return false;
    document.notes.clear();
    document.notes.reserve(10001);
    for (int index = 0; index < 10000; ++index)
        document.notes.append({index, QStringLiteral("其他"), QString::number(index)});
    if (!Expect(review::saveDocument(path, document, error)
                    && review::loadDocument(path, loaded, error) && equal(loaded, document),
                "exactly 10000 notes round trip"))
        return false;
    const QByteArray full = readFile(path);
    document.notes.append({0, QStringLiteral("其他"), QStringLiteral("too many")});
    if (!Expect(!review::saveDocument(path, document, error) && !error.isEmpty()
                    && readFile(path) == full,
                "10001 notes reject save without changing the destination"))
        return false;
    QJsonObject root = validJson();
    QJsonArray tooMany;
    const QJsonValue note = root.value(QStringLiteral("notes")).toArray().first();
    for (int index = 0; index < 10001; ++index)
        tooMany.append(note);
    root.insert(QStringLiteral("notes"), tooMany);
    if (!Expect(rejectsWithoutMutation(path, QJsonDocument(root).toJson(), loaded),
                "10001 notes also reject load without changing its output"))
        return false;

    QByteArray maximum = QJsonDocument(validJson()).toJson(QJsonDocument::Compact);
    maximum.append(QByteArray(64 * 1024 * 1024 - maximum.size(), ' '));
    if (!Expect(writeFile(path, maximum) && review::loadDocument(path, loaded, error),
                "exactly 64 MiB of otherwise valid JSON is accepted"))
        return false;
    maximum.append(' ');
    if (!Expect(rejectsWithoutMutation(path, maximum, loaded),
                "input exceeding 64 MiB is rejected without changing the document"))
        return false;
    return true;
}

bool recoveryRoundTrip(const QDir& directory)
{
    const QString recoveryPath = directory.filePath(QStringLiteral("state/recovery.review.json"));
    review::ReviewRecovery recovery{
        directory.filePath(QStringLiteral("project/current.review.json")),
        {directory.filePath(QStringLiteral("media/video.mp4")),
         {{1234, QStringLiteral("问题"), QStringLiteral("尚未保存的标注")}}}};
    QString error;
    if (!Expect(review::saveRecovery(recoveryPath, recovery, error) && error.isEmpty(),
                "recovery save creates its parent directory"))
        return false;
    review::ReviewRecovery loaded;
    if (!Expect(review::loadRecovery(recoveryPath, loaded, error) && error.isEmpty()
                    && loaded.projectPath == recovery.projectPath
                    && equal(loaded.document, recovery.document),
                "recovery round trip preserves absolute project and media paths"))
        return false;
    const review::ReviewRecovery original = loaded;
    if (!Expect(writeFile(recoveryPath, QByteArray("{"))
                    && !review::loadRecovery(recoveryPath, loaded, error)
                    && loaded.projectPath == original.projectPath
                    && equal(loaded.document, original.document),
                "invalid recovery leaves the destination unchanged"))
        return false;
    review::ReviewRecovery invalid = recovery;
    invalid.projectPath = QStringLiteral("relative.review.json");
    return Expect(!review::saveRecovery(recoveryPath, invalid, error) && !error.isEmpty(),
                  "recovery rejects relative project paths");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!Expect(temporary.isValid(), "temporary test directory is available"))
        return 1;
    if (!Expect(review::formatTime(0) == QStringLiteral("00:00:00.000")
                    && review::formatTime(3723004) == QStringLiteral("01:02:03.004")
                    && review::formatTime(360000000) == QStringLiteral("100:00:00.000")
                    && review::formatTime(-1) == QStringLiteral("00:00:00.000")
                    && !review::formatTime(std::numeric_limits<qint64>::max()).isEmpty(),
                "time formatting preserves milliseconds and does not wrap at 24 hours"))
        return 1;
    const QDir directory(temporary.path());
    return roundTripAndCsv(directory) && validationAndFailure(directory) && boundaries(directory)
        && recoveryRoundTrip(directory) ? 0 : 1;
}
