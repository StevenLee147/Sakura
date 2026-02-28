#pragma once

#include <spdlog/spdlog.h>
#include <string>

namespace sakura::utils
{

class Logger
{
public:
    static void Init(const std::string& logFilePath);
    static void Shutdown();

    static std::shared_ptr<spdlog::logger>& GetLogger() { return s_logger; }

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace sakura::utils

// 全局日志宏
#define LOG_TRACE(...)  ::sakura::utils::Logger::GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)  ::sakura::utils::Logger::GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)   ::sakura::utils::Logger::GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)   ::sakura::utils::Logger::GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)  ::sakura::utils::Logger::GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::sakura::utils::Logger::GetLogger()->critical(__VA_ARGS__)
