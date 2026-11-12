#ifndef LOGGER_IMPL_H
#define LOGGER_IMPL_H

#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <atomic>
#include <iostream>
#include "custom_rotating_file_sink.h"
#include "logger.h"

using json = nlohmann::json;

class Logger::LoggerImpl
{
private:
    struct LoggerConfig {
        explicit LoggerConfig(const std::string &json_path)
            : file_name("./"), type("console"), level("trace"), max_file_size(60),
              max_backup_index(5), buffer_size(8 * 1024), is_async(false),
              flush_on_level("error"), log_console_level("info"), log_file_level("info"),
              log_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%# %!] %v"), is_valid(true)
        {

            auto warning_message = [](const std::string &message) {
                std::cerr << "\n" << std::string(80, '!') << "\n";
                std::cerr << "[WARNING] " << message << "\n";
                std::cerr << std::string(80, '!') << "\n" << std::endl;
            };

            std::ifstream in_file(json_path);
            if (!in_file.is_open()) {
                warning_message("Failed to open config file: " + json_path
                                + ", using default config");
                is_valid = false;
                return;
            }

            json j;
            json logger_config;

            try {
                in_file >> j;
                logger_config = j.value("LoggerConfig", json::object());

                file_name = logger_config.value("FileName", file_name);
                type = logger_config.value("LogType", type);
                level = logger_config.value("LogLevel", level);
                max_file_size = logger_config.value("MaxFileSize", max_file_size);
                max_backup_index = logger_config.value("MaxBackupIndex", max_backup_index);
                is_async = logger_config.value("IsAsync", is_async);
                buffer_size = logger_config.value("BufferSize", 8 * 1024);
                flush_on_level = logger_config.value("FlushOnLevel", flush_on_level);
                log_console_level = logger_config.value("ConsoleLogLevel", log_console_level);
                log_file_level = logger_config.value("FileLogLevel", log_file_level);
                log_pattern = logger_config.value("LogPattern", log_pattern);

            } catch (const json::exception &e) {
                std::cerr << "Failed to parse JSON config: {}, using default config" << e.what()
                          << std::endl;
                is_valid = false;
                return;
            }
        }

        Logger::LoggerType GetType() const
        {
            if (type == "console") {
                return Logger::console;
            } else if (type == "file") {
                return Logger::file;
            } else if (type == "both") {
                return Logger::both;
            } else {
                return Logger::console;
            }
        }

        Logger::SeverityLevel GetLevel(const std::string &level_str) const
        {
            if (level_str == "trace") {
                return Logger::trace;
            } else if (level_str == "debug") {
                return Logger::debug;
            } else if (level_str == "info") {
                return Logger::info;
            } else if (level_str == "warning") {
                return Logger::warning;
            } else if (level_str == "error") {
                return Logger::error;
            } else if (level_str == "critical") {
                return Logger::critical;
            } else {
                return Logger::info;
            }
        }

        Logger::SeverityLevel GetLogLevel() const { return GetLevel(level); }
        Logger::SeverityLevel GetFlushOnLevel() const { return GetLevel(flush_on_level); }
        Logger::SeverityLevel GetConsoleLogLevel() const { return GetLevel(log_console_level); }
        Logger::SeverityLevel GetFileLogLevel() const { return GetLevel(log_file_level); }
        std::string GetLogPattern() const { return log_pattern; }
        std::string GetFileName() const { return file_name; }
        bool GetIsAsync() const { return is_async; }
        uint32_t GetBufferSize() const { return buffer_size; }
        uint32_t GetMaxFileSize() const { return max_file_size; }
        uint32_t GetMaxBackupIndex() const { return max_backup_index; }
        bool GetIsValid() const { return is_valid; }

        void PrintConfig() const
        {
            spdlog::info("LoggerConfig: fileName={}, type={}, level={}, maxFileSize={}, "
                         "maxBackupIndex={}, isAsync={}, flushOnLevel={}, LogConsoleLevel={}, "
                         "LogFileLevel={}, LogPattern={}",
                         file_name, type, level, max_file_size, max_backup_index, is_async,
                         flush_on_level, log_console_level, log_file_level, log_pattern);
        }

    private:
        std::string file_name;
        std::string type;
        std::string level;
        uint32_t max_file_size;
        uint32_t max_backup_index;
        uint32_t buffer_size;
        bool is_async;
        std::string flush_on_level;
        std::string log_console_level;
        std::string log_file_level;
        std::string log_pattern;
        bool is_valid;
    };

public:
    LoggerImpl();
    ~LoggerImpl();

    bool Init(const std::string &file_name, Logger::LoggerType type, Logger::SeverityLevel level,
              uint32_t max_file_size, uint32_t max_backup_index, bool is_async);
    bool Init(const std::string &log_config_file_path);

    void Log(Logger::SeverityLevel level, std::string &&msg, const char *file, uint32_t line,
             const char *func);

    bool ShouldLog(Logger::SeverityLevel level) const;

    void Uninit();
    void SetFlushEvery(uint32_t flush_every);
    void SetFlushOnLevel(Logger::SeverityLevel flush_on_level);
    void SetLogLevel(Logger::SeverityLevel level);
    void SetLogPattern(const std::string &pattern);
    void SetLogConsoleLevel(Logger::SeverityLevel level);
    void SetLogFileLevel(Logger::SeverityLevel level);
    void SetLogBufferSize(size_t size);

private:
    std::shared_ptr<spdlog::logger> logger_ = nullptr;
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink_ = nullptr;
    std::shared_ptr<spdlog::sinks::custom_rotating_file_sink_mt> file_sink_ = nullptr;
    size_t log_buffer_size_;
    std::atomic<bool> is_running_;
    std::string log_config_file_path_;

    spdlog::level::level_enum GetLogLevelFromEnv(const std::string &env_var,
                                                 spdlog::level::level_enum default_level);
    void LoggerConfigChecker();
    void Stop();
    void LogApplyConfig(const LoggerConfig &config);
};

#endif // LOGGER_IMPL_H