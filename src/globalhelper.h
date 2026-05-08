/*
 * @file 	globalhelper.h
 * @date 	2018/01/07 10:41
 *
 * @author 	itisyang
 * @Contact	itisyang@gmail.com
 *
 * @brief 	公共接口
 * @note
 */
#ifndef GLOBALHELPER_H
#define GLOBALHELPER_H

enum ERROR_CODE { NoError = 0, ErrorFileInvalid };

#include <QDebug>
#include <QPushButton>
#include <QString>
#include <QStringList>

class GlobalHelper {
public:
	GlobalHelper();
	/**
	 * 获取样式表
	 *
	 * @param	strQssPath 样式表文件路径
	 * @return	样式表
	 * @note
	 */
	static QString GetQssStr(QString strQssPath);

	/**
	 * 加载完整的设计系统样式（包含全局样式 + 组件特定样式）
	 *
	 * @param	componentQssPath 组件特定样式文件路径（可选）
	 * @return	完整样式表
	 * @note
	 */
	static QString GetThemeStr(QString componentQssPath = QString());

	/**
	 * 为按钮设置显示图标
	 *
	 * @param	btn 按钮指针
	 * @param	iconSize 图标大小
	 * @param	icon 图标字符
	 */
	static void SetIcon(QPushButton* btn, int iconSize, QChar icon);
	static void SetIcon(QPushButton* btn, int iconSize, const QIcon& icon, QString strToolTip = QString());

	static void SavePlaylist(QStringList& playList);
	static void GetPlaylist(QStringList& playList);
	static void SavePlayVolume(double& nVolume);
	static void GetPlayVolume(double& nVolume);

	// 新增：窗口状态持久化
	static void SaveWindowState(const QByteArray& geometry, const QByteArray& windowState);
	static void RestoreWindowState(QByteArray& geometry, QByteArray& windowState);

	// 新增：播放设置
	static void SavePlaySettings(double volume, int loopPolicy, double speed);
	static void LoadPlaySettings(double& volume, int& loopPolicy, double& speed);

	// 新增：最近打开的文件
	static void SaveRecentFiles(const QStringList& recentFiles);
	static void GetRecentFiles(QStringList& recentFiles);
	static void SavePlaybackPosition(const QString& filePath, int seconds);
	static int GetPlaybackPosition(const QString& filePath);

	static QString GetAppVersion();

	/**
	 * 格式化时间为 HH:MM:SS 格式
	 *
	 * @param	seconds 秒数
	 * @return	格式化后的时间字符串
	 * @note	例如：3661 -> "01:01:01"
	 */
	static QString FormatTime(int seconds);

private:
	static QString GetConfigFilePath();
};

#define MAX_SLIDER_VALUE 65536

#endif // GLOBALHELPER_H
