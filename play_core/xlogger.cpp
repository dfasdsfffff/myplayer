#include "xlogger.h"
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cstdio>

static std::mutex g_log_mutex;
static std::ofstream g_log_ofs;
static XLogger::Level g_log_level = XLogger::Level::Info;
static bool g_log_console = true;
static std::string g_log_filename;
static size_t g_log_max_size = 10 * 1024 * 1024;
static int g_log_max_files = 3;
static size_t g_log_cur_size = 0;

bool XLogger::Init(const std::string& logFile, Level level, size_t maxFileSize, int maxFiles, bool toConsole)
{
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_log_level = level;
    g_log_console = toConsole;
    g_log_max_size = maxFileSize;
    g_log_max_files = (maxFiles >= 1) ? maxFiles : 1;
    g_log_filename = logFile;

    if (!g_log_filename.empty()) {
        g_log_ofs.open(g_log_filename, std::ios::out | std::ios::app | std::ios::binary);
        if (!g_log_ofs.is_open()) {
            return false;
        }
        g_log_ofs.seekp(0, std::ios::end);
        std::streampos pos = g_log_ofs.tellp();
        g_log_cur_size = (pos > 0) ? static_cast<size_t>(pos) : 0;
    }
    return true;
}

void XLogger::Shutdown()
{
    std::lock_guard<std::mutex> lk(g_log_mutex);
    if (g_log_ofs.is_open()) {
        g_log_ofs.flush();
        g_log_ofs.close();
    }
    g_log_filename.clear();
}

void XLogger::SetLevel(Level level)
{
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_log_level = level;
}

void XLogger::EnableConsole(bool enable)
{
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_log_console = enable;
}

static std::string CurrentTimeString()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

const char* XLogger::LevelName(Level lvl)
{
    switch (lvl) {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO ";
    case Level::Warn:  return "WARN ";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
    default: return "UNK  ";
    }
}

std::string XLogger::FormatString(const char* fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0) return std::string();
    std::string buf;
    buf.resize(len + 1);
    vsnprintf(&buf[0], len + 1, fmt, ap);
    buf.resize(len);
    return buf;
}

void XLogger::Log(Level lvl, const char* file, int line, const char* func, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string msg = FormatString(fmt, ap);
    va_end(ap);

    std::ostringstream oss;
    oss << CurrentTimeString() << " [" << LevelName(lvl) << "] "
        << file << ":" << line << " (" << func << ") - " << msg << '\n';
    std::string out = oss.str();

    // 单次加锁，同时完成检查和写入操作
    std::lock_guard<std::mutex> lk(g_log_mutex);

    if (lvl < g_log_level) return;  // 检查日志级别

    if (g_log_console) {
        std::fwrite(out.data(), 1, out.size(), stdout);
    }

    if (!g_log_filename.empty() && g_log_ofs.is_open()) {
        g_log_ofs.write(out.data(), static_cast<std::streamsize>(out.size()));
        g_log_ofs.flush();
        g_log_cur_size += out.size();

        // 滚动日志：当文件大小超过限制时，移动并重命名文件
        if (g_log_cur_size >= g_log_max_size) {
            g_log_ofs.close();
            // 从后往前移动备份文件：base.(n-1) -> base.n
            for (int i = g_log_max_files - 1; i >= 1; --i) {
                std::string src = g_log_filename + "." + std::to_string(i);
                std::string dst = g_log_filename + "." + std::to_string(i + 1);
                std::remove(dst.c_str()); // ignore error
                std::rename(src.c_str(), dst.c_str());
            }
            // base -> base.1
            std::string firstBackup = g_log_filename + ".1";
            std::remove(firstBackup.c_str());
            std::rename(g_log_filename.c_str(), firstBackup.c_str());
            // reopen base file
            g_log_ofs.open(g_log_filename, std::ios::out | std::ios::trunc | std::ios::binary);
            g_log_cur_size = 0;
        }
    }
}

void XLogger::Log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string msg = FormatString(fmt, ap);
    va_end(ap);

    // 无源信息时使用 Info 级别，file/line/func 使用占位符
    Log(Level::Info, "unknown", 0, "unknown", "%s", msg.c_str());
}

