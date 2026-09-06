#pragma once
// 轻量分级日志：控制台输出，错误走 stderr。
//
// 商业化增强：
//   - 可配置最低日志级别（SetLogLevel），高于/等于该级别的消息才输出，便于发布版关闭 Debug。
//   - 自动附加时间戳（HH:MM:SS.mmm），便于定位事件时序。
//   - 通过全局互斥锁串行化输出，避免多线程（如 job 系统/线程池）日志交叠。
//   - 保持既有接口兼容：LogMessage(level, msg) 与 LOG_DEBUG/INFO/WARN/ERROR 宏不变。
//
// 线程安全性：本头文件为 inline/静态持锁，多线程调用安全（锁粒度到整条消息）。

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <ctime>

namespace BigHero
{

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

// 仅供内部使用：进程级日志级别存储（函数内静态，规避跨翻译单元初始化顺序问题）。
// 先声明，避免 SetLogLevel/GetLogLevel 使用前置。
inline LogLevel& g_logLevel()
{
    static LogLevel level = LogLevel::Debug;
    return level;
}

// 设置全局最低日志级别：低于该级别（如 Debug 在发布 build）的消息被丢弃。
inline void SetLogLevel(LogLevel level) noexcept
{
    g_logLevel() = level;
}
[[nodiscard]] inline LogLevel GetLogLevel() noexcept
{
    return g_logLevel();
}

// 仅供内部使用：把 level 与当前最低级别比较，决定是否输出。
inline bool g_shouldLog(LogLevel level) noexcept
{
    return static_cast<int>(level) >= static_cast<int>(g_logLevel());
}

// 仅供内部使用：输出一条带时间戳的日志。
inline void g_logWrite(LogLevel level, const std::string& msg)
{
    if (!g_shouldLog(level))
        return;

    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);

    // 时间戳 HH:MM:SS.mmm（本地时间）
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char ts[32] = {};
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tmBuf);

    const char* tag = "";
    switch (level)
    {
    case LogLevel::Debug: tag = "[DEBUG]"; break;
    case LogLevel::Info:  tag = "[INFO ]"; break;
    case LogLevel::Warn:  tag = "[WARN ]"; break;
    case LogLevel::Error: tag = "[ERROR]"; break;
    }

    std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
    out << ts << '.' << std::setfill('0') << std::setw(3) << ms.count() << ' '
        << tag << ' ' << msg << '\n';
    out.flush();
}

inline void LogMessage(LogLevel level, const std::string& msg)
{
    g_logWrite(level, msg);
}

} // namespace BigHero

#define BIGHERO_LOG(level, msg) ::BigHero::LogMessage(level, (std::ostringstream{} << msg).str())

#define LOG_DEBUG(msg) BIGHERO_LOG(::BigHero::LogLevel::Debug, msg)
#define LOG_INFO(msg) BIGHERO_LOG(::BigHero::LogLevel::Info, msg)
#define LOG_WARN(msg) BIGHERO_LOG(::BigHero::LogLevel::Warn, msg)
#define LOG_ERROR(msg) BIGHERO_LOG(::BigHero::LogLevel::Error, msg)
