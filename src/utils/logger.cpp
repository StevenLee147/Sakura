#include "logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/sink.h>
#include <filesystem>

namespace sakura::utils
{

std::shared_ptr<spdlog::logger> Logger::s_logger;

void Logger::Init(const std::string& logFilePath)
{
    // 确保日志目录存在
    std::filesystem::path logPath(logFilePath);
    if (logPath.has_parent_path())
    {
        std::filesystem::create_directories(logPath.parent_path());
    }

    std::vector<spdlog::sink_ptr> sinks;

    // 彩色控制台输出 sink
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::trace);
    consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    sinks.push_back(consoleSink);

    // 轮转文件 sink（5MB，保留3个文件）
    constexpr size_t MAX_FILE_SIZE = 5 * 1024 * 1024;
    constexpr size_t MAX_FILES = 3;
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFilePath, MAX_FILE_SIZE, MAX_FILES);
    fileSink->set_level(spdlog::level::trace);
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    sinks.push_back(fileSink);

    s_logger = std::make_shared<spdlog::logger>("Sakura", sinks.begin(), sinks.end());
    s_logger->set_level(spdlog::level::trace);
    s_logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(s_logger);
    spdlog::set_default_logger(s_logger);

    LOG_INFO("Logger initialized. Log file: {}", logFilePath);
}

void Logger::Shutdown()
{
    spdlog::shutdown();
}

} // namespace sakura::utils
