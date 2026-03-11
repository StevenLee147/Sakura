#pragma once

#include <string>

#ifndef SAKURA_HAS_SPDLOG
#define SAKURA_HAS_SPDLOG 1
#endif

#if SAKURA_HAS_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace sakura::utils
{

class Logger
{
public:
    static void Init(const std::string& logFilePath);
    static void Shutdown();

#if SAKURA_HAS_SPDLOG
    static std::shared_ptr<spdlog::logger>& GetLogger() { return s_logger; }

private:
    static std::shared_ptr<spdlog::logger> s_logger;
#endif
};

} // namespace sakura::utils

// 全局日志宏
#if SAKURA_HAS_SPDLOG
#define LOG_TRACE(...)  ::sakura::utils::Logger::GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)  ::sakura::utils::Logger::GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)   ::sakura::utils::Logger::GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)   ::sakura::utils::Logger::GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)  ::sakura::utils::Logger::GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::sakura::utils::Logger::GetLogger()->critical(__VA_ARGS__)
#else
#define LOG_TRACE(...)  ((void)0)
#define LOG_DEBUG(...)  ((void)0)
#define LOG_INFO(...)   ((void)0)
#define LOG_WARN(...)   ((void)0)
#define LOG_ERROR(...)  ((void)0)
#define LOG_CRITICAL(...) ((void)0)
#endif
