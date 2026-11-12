#include "logger_impl.h"

#include <pthread.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

Logger::LoggerImpl::LoggerImpl() 
    : logger_(nullptr)
    , console_sink_(nullptr)
    , file_sink_(nullptr)
    , log_buffer_size_(8192)
    , is_running_(true)
    , log_config_file_path_()
{
}

Logger::LoggerImpl::~LoggerImpl()
{
    Uninit();
    try {
        spdlog::shutdown();
    } catch (...) {
    }
}

void Logger::LoggerImpl::LoggerConfigChecker()
{
    fs::file_time_type last_write_time;
    pthread_setname_np(pthread_self(), "LogConfigCheck");
    while (is_running_) {
        try {
            fs::file_time_type current_write_time = fs::last_write_time(log_config_file_path_);
            if (current_write_time != last_write_time) {
                last_write_time = current_write_time;
                LoggerConfig config(log_config_file_path_);
                if (config.GetIsValid()) {
                    LogApplyConfig(config);
                } else {
                    break; // TODO(wwk): Better error handling needed
                }
            }
        } catch (const fs::filesystem_error &) {
            // Ignore filesystem errors and continue monitoring
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void Logger::LoggerImpl::LogApplyConfig(const LoggerConfig &config)
{
    SetFlushOnLevel(config.GetFlushOnLevel());
    SetLogConsoleLevel(config.GetConsoleLogLevel());
    SetLogFileLevel(config.GetFileLogLevel());
}

bool Logger::LoggerImpl::Init(const std::string &log_config_file_path)
{
    log_config_file_path_ = log_config_file_path;
    LoggerConfig config(log_config_file_path);
    Logger::LoggerType type = config.GetType();
    Logger::SeverityLevel level = config.GetLogLevel();
    std::string file_name = config.GetFileName();
    uint32_t max_file_size = config.GetMaxFileSize();
    uint32_t max_backup_index = config.GetMaxBackupIndex();
    bool is_async = config.GetIsAsync();

    if (!Init(file_name, type, level, max_file_size, max_backup_index, is_async)) {
        return false;
    }

    SetFlushOnLevel(config.GetFlushOnLevel());
    SetLogPattern(config.GetLogPattern());
    SetLogBufferSize(config.GetBufferSize());

    spdlog::level::level_enum console_level =
        GetLogLevelFromEnv("LOG_CONSOLE_LEVEL", static_cast<spdlog::level::level_enum>(level));
    spdlog::level::level_enum file_level =
        GetLogLevelFromEnv("LOG_FILE_LEVEL", static_cast<spdlog::level::level_enum>(level));

    SetLogConsoleLevel(static_cast<Logger::SeverityLevel>(console_level));
    SetLogFileLevel(static_cast<Logger::SeverityLevel>(file_level));

    // Start monitoring thread
    std::thread([this]() { LoggerConfigChecker(); }).detach();
    return true;
}

void Logger::LoggerImpl::Stop()
{
    is_running_ = false;
}

bool Logger::LoggerImpl::Init(const std::string &file_name, Logger::LoggerType type,
                              Logger::SeverityLevel level, uint32_t max_file_size,
                              uint32_t max_backup_index, bool is_async)
{
    if (spdlog::get("Logger")) {
        spdlog::warn("Logger already initialized, skipping re-initialization");
        return false;
    }

    std::size_t max_file_size_bytes = 1024 * 1024 * max_file_size;
    std::vector<spdlog::sink_ptr> sinks;

    spdlog::level::level_enum console_level =
        GetLogLevelFromEnv("LOG_CONSOLE_LEVEL", static_cast<spdlog::level::level_enum>(level));
    spdlog::level::level_enum file_level =
        GetLogLevelFromEnv("LOG_FILE_LEVEL", static_cast<spdlog::level::level_enum>(level));

    switch (type) {
        case Logger::both: {
            console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            file_sink_ = std::make_shared<spdlog::sinks::custom_rotating_file_sink_mt>(
                file_name, max_file_size_bytes, max_backup_index);
            console_sink_->set_level(console_level);
            file_sink_->set_level(file_level);
            sinks.push_back(file_sink_);
            sinks.push_back(console_sink_);
            break;
        }
        case Logger::console: {
            console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink_->set_level(console_level);
            sinks.push_back(console_sink_);
            break;
        }
        case Logger::file: {
            file_sink_ = std::make_shared<spdlog::sinks::custom_rotating_file_sink_mt>(
                file_name, max_file_size_bytes, max_backup_index);
            file_sink_->set_level(file_level);
            sinks.push_back(file_sink_);
            break;
        }
        default:
            console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink_->set_level(console_level);
            sinks.push_back(console_sink_);
            break;
    }

    if (sinks.empty()) {
        spdlog::error("No sinks were created for the logger");
        return false;
    }

    if (is_async) {
        log_buffer_size_ = std::max(log_buffer_size_, static_cast<size_t>(64 * 1024)); //64k
        size_t thread_count = std::max(1u, std::thread::hardware_concurrency() / 4);   // 线程池
        
        // 尝试初始化或获取已存在的线程池
        bool thread_pool_available = false;
        try {
            spdlog::init_thread_pool(log_buffer_size_, thread_count);
            thread_pool_available = true;
        } catch (const spdlog::spdlog_ex &e) {
            // 线程池可能已存在,尝试获取
            try {
                auto tp = spdlog::thread_pool();
                if (tp) {
                    thread_pool_available = true;
                }
            } catch (...) {
                std::cerr << "Warning: Failed to get thread pool, falling back to sync logger" << std::endl;
            }
        }

        if (thread_pool_available) {
            logger_ = std::make_shared<spdlog::async_logger>(
                "Logger", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                spdlog::async_overflow_policy::overrun_oldest); // 覆盖策略而非阻塞
        } else {
            std::cerr << "Warning: Using synchronous logger instead of async" << std::endl;
            logger_ = std::make_shared<spdlog::logger>("Logger", sinks.begin(), sinks.end());
        }
        } else {
        logger_ = std::make_shared<spdlog::logger>("Logger", sinks.begin(), sinks.end());
    }

    logger_->set_level(spdlog::level::trace);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%# %!] %v");

    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
    return true;
}

void Logger::LoggerImpl::Uninit()
{
    Stop();
    if (logger_) {
        logger_->flush();
        spdlog::drop("Logger");
        logger_.reset();
    }
    try {
        spdlog::shutdown();
    } catch (...) {
    }
}

void Logger::LoggerImpl::Log(Logger::SeverityLevel level, std::string &&msg, const char *file,
                             uint32_t line, const char *func)
{
    if (logger_) {
        logger_->log(spdlog::source_loc{file, static_cast<int>(line), func},
                     static_cast<spdlog::level::level_enum>(level), std::move(msg));
    }
}

bool Logger::LoggerImpl::ShouldLog(Logger::SeverityLevel level) const
{
    return logger_ && logger_->should_log(static_cast<spdlog::level::level_enum>(level));
}

spdlog::level::level_enum
Logger::LoggerImpl::GetLogLevelFromEnv(const std::string &env_var,
                                       spdlog::level::level_enum default_level)
{
    const char *env_value = std::getenv(env_var.c_str());
    if (!env_value) {
        return default_level;
    }
    std::string level_str(env_value);
    return spdlog::level::from_str(level_str);
}

void Logger::LoggerImpl::SetFlushEvery(uint32_t flush_every)
{
    if (logger_) {
        spdlog::flush_every(std::chrono::seconds(flush_every));
    }
}

void Logger::LoggerImpl::SetFlushOnLevel(Logger::SeverityLevel flush_on_level)
{
    if (logger_) {
        logger_->flush_on(static_cast<spdlog::level::level_enum>(flush_on_level));
    }
}

void Logger::LoggerImpl::SetLogLevel(Logger::SeverityLevel level)
{
    if (logger_) {
        logger_->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

void Logger::LoggerImpl::SetLogPattern(const std::string &pattern)
{
    if (logger_) {
        logger_->set_pattern(pattern);
    }
}

void Logger::LoggerImpl::SetLogConsoleLevel(Logger::SeverityLevel level)
{
    if (console_sink_) {
        console_sink_->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

void Logger::LoggerImpl::SetLogFileLevel(Logger::SeverityLevel level)
{
    if (file_sink_) {
        file_sink_->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

void Logger::LoggerImpl::SetLogBufferSize(size_t size)
{
    log_buffer_size_ = size;
}