#include "review_document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringView>

#include <cmath>
#include <utility>

namespace review {
namespace {

constexpr qint64 MaxFileBytes = 64 * 1024 * 1024;
constexpr qsizetype MaxNotes = 10000;
constexpr qsizetype MaxTextUnits = 10000;
constexpr qint64 MaxPositionMs = 9007199254740991LL;

bool fail(QString& error, const QString& message)
{
    error = message;
    return false;
}

bool validCategory(const QString& category)
{
    return category == QStringLiteral("问题") || category == QStringLiteral("建议")
        || category == QStringLiteral("重点") || category == QStringLiteral("其他");
}

bool validateNote(const ReviewNote& note, QString& error)
{
    if (note.positionMs < 0 || note.positionMs > MaxPositionMs)
        return fail(error, QStringLiteral("标注时间超出有效范围。"));
    if (!validCategory(note.category))
        return fail(error, QStringLiteral("标注类别无效。"));
    if (QStringView(note.text).trimmed().isEmpty() || note.text.size() > MaxTextUnits)
        return fail(error, QStringLiteral("标注文字不能为空且不能超过 10000 个 UTF-16 单元。"));
    return true;
}

bool validateDocument(const ReviewDocument& document, QString& error)
{
    if (QStringView(document.mediaPath).trimmed().isEmpty() || document.mediaPath.contains(QChar::Null)
        || !QDir::isAbsolutePath(document.mediaPath))
        return fail(error, QStringLiteral("媒体路径必须是非空的绝对本地路径。"));
    if (document.notes.size() > MaxNotes)
        return fail(error, QStringLiteral("标注数量不能超过 10000 条。"));
    for (const ReviewNote& note : document.notes) {
        if (!validateNote(note, error))
            return false;
    }
    return true;
}

QString resolvedPath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
        return canonical;
    const QString parent = info.dir().canonicalPath();
    return parent.isEmpty() ? QDir::cleanPath(info.absoluteFilePath())
                            : QDir(parent).filePath(info.fileName());
}

bool writableDestination(const QString& path, const ReviewDocument& document, QString& error)
{
    if (QStringView(path).trimmed().isEmpty() || path.contains(QChar::Null))
        return fail(error, QStringLiteral("输出路径不能为空。"));
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    if (resolvedPath(path).compare(resolvedPath(document.mediaPath), sensitivity) == 0)
        return fail(error, QStringLiteral("不能覆盖原视频文件。"));
    return true;
}

bool writeBytes(QSaveFile& file, const QByteArray& bytes, QString& error)
{
    if (file.write(bytes) != bytes.size())
        return fail(error, QStringLiteral("写入失败：%1").arg(file.errorString()));
    return true;
}

bool commit(QSaveFile& file, QString& error)
{
    if (!file.commit())
        return fail(error, QStringLiteral("保存失败：%1").arg(file.errorString()));
    error.clear();
    return true;
}

QByteArray csvText(const QString& text)
{
    // Spreadsheet programs can ignore leading whitespace before a formula.
    qsizetype start = 0;
    while (start < text.size() && text.at(start).isSpace()
           && text.at(start) != QLatin1Char('\t') && text.at(start) != QLatin1Char('\r'))
        ++start;
    const bool formula = start < text.size()
        && (text.at(start) == QLatin1Char('=') || text.at(start) == QLatin1Char('+')
            || text.at(start) == QLatin1Char('-') || text.at(start) == QLatin1Char('@')
            || text.at(start) == QLatin1Char('\t') || text.at(start) == QLatin1Char('\r'));
    QByteArray escaped = text.toUtf8();
    escaped.replace("\"", "\"\"");
    return QByteArray("\"") + (formula ? QByteArray("'") : QByteArray()) + escaped + '"';
}

} // namespace

QString formatTime(qint64 milliseconds)
{
    const qint64 value = milliseconds < 0 ? 0 : milliseconds;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(value / 3600000, 2, 10, QLatin1Char('0'))
        .arg((value / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((value / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(value % 1000, 3, 10, QLatin1Char('0'));
}

bool loadDocument(const QString& path, ReviewDocument& document, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("打开工程失败：%1").arg(file.errorString()));
    if (file.size() > MaxFileBytes)
        return fail(error, QStringLiteral("工程文件不能超过 64 MiB。"));
    const QByteArray bytes = file.read(MaxFileBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("读取工程失败：%1").arg(file.errorString()));
    if (bytes.size() > MaxFileBytes || !file.atEnd())
        return fail(error, QStringLiteral("工程文件不能超过 64 MiB。"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return fail(error, QStringLiteral("工程 JSON 无效：%1").arg(parseError.errorString()));
    const QJsonObject root = json.object();
    if (!root.value(QStringLiteral("format")).isString()
        || root.value(QStringLiteral("format")).toString() != QStringLiteral("myplayer-review"))
        return fail(error, QStringLiteral("未知的工程格式。"));
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != 1.0)
        return fail(error, QStringLiteral("不支持的工程版本。"));
    const QJsonValue media = root.value(QStringLiteral("media"));
    const QJsonValue notes = root.value(QStringLiteral("notes"));
    if (!media.isString() || media.toString().trimmed().isEmpty()
        || media.toString().contains(QChar::Null) || !notes.isArray())
        return fail(error, QStringLiteral("工程媒体路径或标注列表类型无效。"));
    const QJsonArray array = notes.toArray();
    if (array.size() > MaxNotes)
        return fail(error, QStringLiteral("标注数量不能超过 10000 条。"));

    ReviewDocument loaded;
    loaded.mediaPath = QDir::cleanPath(QFileInfo(path).absoluteDir().absoluteFilePath(media.toString()));
    loaded.notes.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            return fail(error, QStringLiteral("标注必须是 JSON 对象。"));
        const QJsonObject object = value.toObject();
        const QJsonValue position = object.value(QStringLiteral("positionMs"));
        const QJsonValue category = object.value(QStringLiteral("category"));
        const QJsonValue text = object.value(QStringLiteral("text"));
        if (!position.isDouble() || !category.isString() || !text.isString())
            return fail(error, QStringLiteral("标注时间、类别或文字类型无效。"));
        const double milliseconds = position.toDouble();
        if (!std::isfinite(milliseconds) || milliseconds < 0
            || milliseconds > static_cast<double>(MaxPositionMs)
            || std::floor(milliseconds) != milliseconds)
            return fail(error, QStringLiteral("标注时间必须是 JSON 精确范围内的非负整数。"));
        ReviewNote note{static_cast<qint64>(milliseconds), category.toString(), text.toString()};
        if (!validateNote(note, error))
            return false;
        loaded.notes.append(std::move(note));
    }
    document = std::move(loaded);
    error.clear();
    return true;
}

bool saveDocument(const QString& path, const ReviewDocument& document, QString& error)
{
    if (!validateDocument(document, error) || !writableDestination(path, document, error))
        return false;
    QJsonArray notes;
    for (const ReviewNote& note : document.notes) {
        notes.append(QJsonObject{{QStringLiteral("positionMs"), note.positionMs},
                                 {QStringLiteral("category"), note.category},
                                 {QStringLiteral("text"), note.text}});
    }
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("myplayer-review")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("media"), QFileInfo(path).absoluteDir().relativeFilePath(document.mediaPath)},
        {QStringLiteral("notes"), notes}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFileBytes)
        return fail(error, QStringLiteral("工程文件不能超过 64 MiB。"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("创建工程失败：%1").arg(file.errorString()));
    if (!writeBytes(file, bytes, error))
        return false;
    return commit(file, error);
}

bool exportCsv(const QString& path, const ReviewDocument& document, QString& error)
{
    if (!validateDocument(document, error) || !writableDestination(path, document, error))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("创建 CSV 失败：%1").arg(file.errorString()));
    if (!writeBytes(file, QByteArray("\xEF\xBB\xBFmedia,time_ms,timecode,category,text\r\n"), error))
        return false;
    const QByteArray media = csvText(document.mediaPath);
    for (const ReviewNote& note : document.notes) {
        const QByteArray row = media + ',' + QByteArray::number(note.positionMs) + ','
            + csvText(formatTime(note.positionMs)) + ',' + csvText(note.category) + ','
            + csvText(note.text) + "\r\n";
        if (!writeBytes(file, row, error))
            return false;
    }
    return commit(file, error);
}

bool saveRecovery(const QString& path, const ReviewRecovery& recovery, QString& error)
{
    if (!validateDocument(recovery.document, error))
        return false;
    if (!recovery.projectPath.isEmpty() && !QFileInfo(recovery.projectPath).isAbsolute())
        return fail(error, QStringLiteral("恢复记录中的工程路径必须是绝对路径。"));
    QJsonArray notes;
    for (const ReviewNote& note : recovery.document.notes) {
        notes.append(QJsonObject{{QStringLiteral("positionMs"), note.positionMs},
                                 {QStringLiteral("category"), note.category},
                                 {QStringLiteral("text"), note.text}});
    }
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("myplayer-review-recovery")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("project"), recovery.projectPath},
        {QStringLiteral("media"), recovery.document.mediaPath},
        {QStringLiteral("notes"), notes}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFileBytes)
        return fail(error, QStringLiteral("恢复文件不能超过 64 MiB。"));
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(error, QStringLiteral("无法创建恢复文件目录。"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("创建恢复文件失败：%1").arg(file.errorString()));
    if (!writeBytes(file, bytes, error))
        return false;
    return commit(file, error);
}

bool loadRecovery(const QString& path, ReviewRecovery& recovery, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("打开恢复文件失败：%1").arg(file.errorString()));
    if (file.size() > MaxFileBytes)
        return fail(error, QStringLiteral("恢复文件不能超过 64 MiB。"));
    const QByteArray bytes = file.read(MaxFileBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("读取恢复文件失败：%1").arg(file.errorString()));
    if (bytes.size() > MaxFileBytes || !file.atEnd())
        return fail(error, QStringLiteral("恢复文件不能超过 64 MiB。"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return fail(error, QStringLiteral("恢复 JSON 无效：%1").arg(parseError.errorString()));
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("myplayer-review-recovery")
        || root.value(QStringLiteral("version")).toDouble(-1) != 1.0)
        return fail(error, QStringLiteral("未知的恢复文件格式或版本。"));
    const QJsonValue project = root.value(QStringLiteral("project"));
    const QJsonValue media = root.value(QStringLiteral("media"));
    const QJsonValue notes = root.value(QStringLiteral("notes"));
    if (!project.isString() || !media.isString() || !notes.isArray()
        || media.toString().trimmed().isEmpty() || media.toString().contains(QChar::Null))
        return fail(error, QStringLiteral("恢复文件中的路径或标注列表无效。"));
    if ((!project.toString().isEmpty() && !QFileInfo(project.toString()).isAbsolute())
        || !QFileInfo(media.toString()).isAbsolute())
        return fail(error, QStringLiteral("恢复文件中的路径必须是绝对路径。"));
    const QJsonArray array = notes.toArray();
    if (array.size() > MaxNotes)
        return fail(error, QStringLiteral("标注数量不能超过 10000 条。"));

    ReviewRecovery loaded;
    loaded.projectPath = project.toString().isEmpty() ? QString() : QDir::cleanPath(project.toString());
    loaded.document.mediaPath = QDir::cleanPath(media.toString());
    loaded.document.notes.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            return fail(error, QStringLiteral("恢复文件中的标注必须是 JSON 对象。"));
        const QJsonObject object = value.toObject();
        const QJsonValue position = object.value(QStringLiteral("positionMs"));
        const QJsonValue category = object.value(QStringLiteral("category"));
        const QJsonValue text = object.value(QStringLiteral("text"));
        if (!position.isDouble() || !category.isString() || !text.isString())
            return fail(error, QStringLiteral("恢复文件中的标注字段类型无效。"));
        const double milliseconds = position.toDouble();
        if (!std::isfinite(milliseconds) || milliseconds < 0
            || milliseconds > static_cast<double>(MaxPositionMs)
            || std::floor(milliseconds) != milliseconds)
            return fail(error, QStringLiteral("恢复文件中的标注时间无效。"));
        ReviewNote note{static_cast<qint64>(milliseconds), category.toString(), text.toString()};
        if (!validateNote(note, error))
            return false;
        loaded.document.notes.append(std::move(note));
    }
    if (!validateDocument(loaded.document, error))
        return false;
    recovery = std::move(loaded);
    error.clear();
    return true;
}

} // namespace review
