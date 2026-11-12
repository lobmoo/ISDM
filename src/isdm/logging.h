#pragma once

#include <cstdint>
#include <string>

#include "isdm/log/logger.h"

// ISDM logging facade over the project's Logger component.
//
// The library is silent until the application initializes the logger:
//
//   isdm::log::init_console();                       // quick start
//   isdm::log::init_file("log/isdm.log");            // file only
//   isdm::log::init_both("log/isdm.log");            // file + console
//   isdm::log::init_from_config("config/log.json");  // JSON config
//
// Use ISDM_LOG(level) for one-shot events and ISDM_LOG_TIME(level, ms) for
// repeated/hot-path errors that need rate limiting.
namespace isdm {
namespace log {

using Level = Logger::SeverityLevel;

inline bool init_console(Level level = Logger::info) {
    return Logger::GetInstance()->Init("", Logger::console, level, 64, 1);
}

inline bool init_file(const std::string& file_name, Level level = Logger::info,
                      std::uint32_t max_file_size_mb = 10,
                      std::uint32_t max_backup_index = 3, bool async = false) {
    return Logger::GetInstance()->Init(file_name, Logger::file, level, max_file_size_mb,
                                       max_backup_index, async);
}

inline bool init_both(const std::string& file_name, Level level = Logger::info,
                      std::uint32_t max_file_size_mb = 10,
                      std::uint32_t max_backup_index = 3, bool async = false) {
    return Logger::GetInstance()->Init(file_name, Logger::both, level, max_file_size_mb,
                                       max_backup_index, async);
}

inline bool init_from_config(const std::string& log_config_file_path) {
    return Logger::GetInstance()->Init(log_config_file_path);
}

inline void set_level(Level level) { Logger::GetInstance()->SetLogLevel(level); }

inline void shutdown() { Logger::GetInstance()->Uninit(); }

}  // namespace log
}  // namespace isdm

#define ISDM_LOG(level) LOG(level)

// Avoid constructing the rate-limiter key when this level is disabled.
#define ISDM_LOG_TIME(level, interval_ms)                                  \
    if (Logger::GetInstance()->ShouldLog(Logger::level))                   \
    LOG_TIME(level, interval_ms)
