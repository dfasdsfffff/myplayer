#pragma once
#include <string>
#include <cstdarg>

// 便捷宏（添加到需要的公共头或直接包含 xlogger.h 后使用）
#ifndef XLOG_DEBUG
#define XLOG_DEBUG(fmt, ...) XLogger::Log(XLogger::Level::Debug, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define XLOG_INFO(fmt, ...)  XLogger::Log(XLogger::Level::Info,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define XLOG_WARN(fmt, ...)  XLogger::Log(XLogger::Level::Warn,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define XLOG_ERROR(fmt, ...) XLogger::Log(XLogger::Level::Error, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define XLOG_FATAL(fmt, ...) XLogger::Log(XLogger::Level::Fatal, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#endif

class XLogger
{
public:
	enum class Level { Debug = 0, Info, Warn, Error, Fatal };
	// 初始化日志（必须在多线程使用前调用一次）
	// logFile: 日志文件路径（空表示不写文件）
	// level: 最低输出级别
	// maxFileSize: 单个日志文件最大字节数，超过则触发滚动
	// maxFiles: 保留多少个备份（>=1）
	// toConsole: 是否同时输出到控制台
	static bool Init(const std::string& logFile, Level level = Level::Info,
		size_t maxFileSize = 10 * 1024 * 1024, int maxFiles = 3, bool toConsole = true);
	static void Shutdown();

	static void SetLevel(Level level);
	static void EnableConsole(bool enable);

	// 主日志函数，包含源信息
	static void Log(Level lvl, const char* file, int line, const char* func, const char* fmt, ...);

	// 向后兼容：默认 Info 级别
	static void Log(const char* fmt, ...);
	//
	static const char* LevelName(Level lvl);
	//
	static std::string FormatString(const char* fmt, va_list ap);
};
