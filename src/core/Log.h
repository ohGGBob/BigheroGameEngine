#pragma once
// 轻量分级日志：控制台输出，错误走stderr
#include <iostream>
#include <sstream>
#include <string>

namespace BigHero
{
enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

inline void LogMessage(LogLevel level, const std::string& msg)
{
    std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
    const char* tag = "";
    switch (level)
    {
    case LogLevel::Debug:
        tag = "[DEBUG]";
        break;
    case LogLevel::Info:
        tag = "[INFO ]";
        break;
    case LogLevel::Warn:
        tag = "[WARN ]";
        break;
    case LogLevel::Error:
        tag = "[ERROR]";
        break;
    }
    out << tag << " " << msg << "\n";
}
} // namespace BigHero

#define BIGHERO_LOG(level, msg) ::BigHero::LogMessage(level, (std::ostringstream{} << msg).str())

#define LOG_DEBUG(msg) BIGHERO_LOG(::BigHero::LogLevel::Debug, msg)
#define LOG_INFO(msg) BIGHERO_LOG(::BigHero::LogLevel::Info, msg)
#define LOG_WARN(msg) BIGHERO_LOG(::BigHero::LogLevel::Warn, msg)
#define LOG_ERROR(msg) BIGHERO_LOG(::BigHero::LogLevel::Error, msg)

