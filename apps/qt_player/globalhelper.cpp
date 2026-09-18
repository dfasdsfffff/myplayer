#include <QFile>
#include <QDebug>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QApplication>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>

#include "globalhelper.h"
#include "media_location_privacy.h"
#include "av_constants.h"

const QString PLAYER_CONFIG = "player_config.ini";

const QString APP_VERSION = "1.0.0";

QString GlobalHelper::GetConfigFilePath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
	static const QString path = [] {
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        const QString configFile = QDir(configDir).filePath(PLAYER_CONFIG);
        const QString legacyFile = QDir(QCoreApplication::applicationDirPath()).filePath("config/" + PLAYER_CONFIG);
        if (!QFile::exists(configFile) && QFile::exists(legacyFile) && QFile::copy(legacyFile, configFile)) {
            QSettings migrated(configFile, QSettings::IniFormat);
            migrated.setValue("migration/version", 1);
            migrated.sync();
        }
        return configFile;
    }();
	return path;
}

QString GlobalHelper::PreferencesFilePath()
{
    return GetConfigFilePath();
}

GlobalHelper::GlobalHelper()
{

}

QString GlobalHelper::GetQssStr(QString strQssPath)
{
    QString strQss;
    QFile FileQss(strQssPath);
    if (FileQss.open(QIODevice::ReadOnly))
    {
        strQss = FileQss.readAll();
        FileQss.close();
    }
    else
    {
        qWarning() << "读取样式表失败" << strQssPath;
    }
    return strQss;
}

QString GlobalHelper::GetThemeStr(QString componentQssPath)
{
    // 加载统一设计系统
    QString designSystem = GetQssStr(":/res/qss/design-system.css");
    
    // 如果有组件特定样式，追加加载
    if (!componentQssPath.isEmpty())
    {
        QString componentQss = GetQssStr(componentQssPath);
        if (!componentQss.isEmpty())
        {
            designSystem += "\n" + componentQss;
        }
    }
    
    return designSystem;
}

void GlobalHelper::SetIcon(QPushButton* btn, int iconSize, QChar icon)
{
    QFont font;
    font.setFamily("FontAwesome");
    font.setPointSize(iconSize);

    btn->setFont(font);
    btn->setText(icon);
}

void GlobalHelper::SetIcon(QPushButton* btn, int iconSize,const QIcon& icon, QString strToolTip)
{
    QFont font;
    font.setFamily("FontAwesome");
    btn->setIconSize({ iconSize, iconSize });

    btn->setFont(font);
    btn->setIcon(icon);
    btn->setToolTip(strToolTip);
}

void GlobalHelper::SavePlaylist(const QStringList& playList)
{
    //QString strPlayerConfigFileName = QCoreApplication::applicationDirPath() + QDir::separator() + PLAYER_CONFIG;
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    settings.remove("playlist");
    settings.beginWriteArray("playlist");
    int persistedIndex = 0;
    for (const QString& location : playList)
    {
        if (!MayPersistMediaLocation(location))
            continue;
        settings.setArrayIndex(persistedIndex++);
        settings.setValue("movie", location);
    }
    settings.endArray();
}

void GlobalHelper::GetPlaylist(QStringList& playList)
{
    //QString strPlayerConfigFileName = QCoreApplication::applicationDirPath() + QDir::separator() + PLAYER_CONFIG;
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);

    int size = settings.beginReadArray("playlist");
    for (int i = 0; i < size; ++i) 
    {
        settings.setArrayIndex(i);
        playList.append(settings.value("movie").toString());
    }
    settings.endArray();
}

void GlobalHelper::SavePlayVolume(double nVolume)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    settings.setValue("volume/size", nVolume);
}

void GlobalHelper::GetPlayVolume(double& nVolume)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    nVolume = settings.value("volume/size", nVolume).toDouble();
}

// 新增：窗口状态持久化
void GlobalHelper::SaveWindowState(const QByteArray& geometry, const QByteArray& windowState)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    settings.setValue("window/geometry", geometry);
    settings.setValue("window/state", windowState);
    settings.sync();
}

void GlobalHelper::RestoreWindowState(QByteArray& geometry, QByteArray& windowState)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    geometry = settings.value("window/geometry").toByteArray();
    windowState = settings.value("window/state").toByteArray();
}

// 新增：播放设置
void GlobalHelper::SavePlaySettings(double volume, int loopPolicy, double speed)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    settings.setValue("play/volume", volume);
    settings.setValue("play/loop_policy", loopPolicy);
    settings.setValue("play/speed", speed);
    settings.sync();
}

void GlobalHelper::LoadPlaySettings(double& volume, int& loopPolicy, double& speed)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    volume = settings.value("play/volume", 1.0).toDouble();
    loopPolicy = settings.value("play/loop_policy", 0).toInt();
    speed = settings.value("play/speed", 1.0).toDouble();
}

// 新增：最近打开的文件
void GlobalHelper::SaveRecentFiles(const QStringList& recentFiles)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    settings.remove("recent_files");
    settings.beginWriteArray("recent_files");
    int persistedIndex = 0;
    for (const QString& location : recentFiles)
    {
        if (!MayPersistMediaLocation(location))
            continue;
        settings.setArrayIndex(persistedIndex++);
        settings.setValue("file", location);
    }
    settings.endArray();
    settings.sync();
}

void GlobalHelper::GetRecentFiles(QStringList& recentFiles)
{
    QString strPlayerConfigFileName = GetConfigFilePath();
    QSettings settings(strPlayerConfigFileName, QSettings::IniFormat);
    int size = settings.beginReadArray("recent_files");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        recentFiles.append(settings.value("file").toString());
    }
    settings.endArray();
}

void GlobalHelper::SavePlaybackPosition(const QString& filePath, int seconds)
{
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    if (canonicalPath.isEmpty())
        return;

    const QByteArray key = QCryptographicHash::hash(canonicalPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    QSettings settings(GetConfigFilePath(), QSettings::IniFormat);
    settings.setValue(QString("playback_position/%1").arg(QString::fromLatin1(key)), seconds);
}

int GlobalHelper::GetPlaybackPosition(const QString& filePath)
{
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    if (canonicalPath.isEmpty())
        return 0;

    const QByteArray key = QCryptographicHash::hash(canonicalPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    QSettings settings(GetConfigFilePath(), QSettings::IniFormat);
    return settings.value(QString("playback_position/%1").arg(QString::fromLatin1(key)), 0).toInt();
}

QString GlobalHelper::GetAppVersion()
{
    return APP_VERSION;
}

QString GlobalHelper::FormatTime(int seconds)
{
    int hh = seconds / SECONDS_PER_HOUR;
    int mm = (seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
    int ss = (seconds % SECONDS_PER_MINUTE);
    
    return QString("%1:%2:%3")
        .arg(hh, 2, 10, QChar('0'))
        .arg(mm, 2, 10, QChar('0'))
        .arg(ss, 2, 10, QChar('0'));
}
