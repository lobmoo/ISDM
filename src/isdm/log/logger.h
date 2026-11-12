#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

/******************************************************** 
 * 日志系统配置文件示例
 * ******************************************************/
/*
{
  "LoggerConfig": {
    "FileName": "./dsfc.log",
    "LogType": "both",
    "LogLevel": "trace",
    "MaxFileSize": 1,
    "MaxBackupIndex": 10,
    "IsAsync": false,
    "BufferSize": 8192,
    "FlushOnLevel": "error",
    "ConsoleLogLevel": "tarce",
    "FileLogLevel": "error",
    "LogPattern": "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%# %!] %v"
  }
}
*/

class LogStreamOptimized;

class Logger
{
public:
    enum SeverityLevel { trace, debug, info, warning, error, critical };
    enum LoggerType { both = 0, console, file };

public:
    ~Logger() = default;

    /**
   * @brief 初始化日志系统
   *
   * @param fileName        日志文件名，支持绝对路径和相对路径
   * @param type           日志类型，可选值：both（文件和控制台）, console（控制台）, file（文件）
   * @param level          日志级别，控制日志的输出级别
   * @param maxFileSize    单个日志文件的最大大小（单位：MB）
   * @param maxBackupIndex 备份日志文件的最大数量
   * @param isAsync        是否启用异步日志，默认为 false（同步模式）  
   * @return true          初始化成功
   * @return false         初始化失败
   */
    bool Init(const std::string &file_name, LoggerType type, SeverityLevel level,
              uint32_t max_file_size, uint32_t max_backup_index, bool is_async = false);

    /**
     * @brief 根据配置文件生成日志器。
     *
     * - 当日志路径无效时，将启用默认配置运行，此时不会写入文件。
     * - 默认参数如下：
     *     type            : "console"
     *     level           : "trace"
     *     maxFileSize     : 60 (MB)
     *     maxBackupIndex  : 5
     *     isAsync         : false
     *     flushOnLevel    : "error"
     *     LogConsoleLevel : "info"
     *     LogFileLevel    : "info"
     *     LogPattern      : "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%# %!] %v"
     *     isValid         : true
     *
     * @param logConfigFilePath 配置文件路径
     * @return true  加载成功
     * @return false 加载失败，使用默认配置
     */
    bool Init(const std::string &log_config_file_path);

    // Uninitialize logger
    void Uninit();

    // Set flush interval in seconds (0 for default mechanism)
    void SetFlushEvery(uint32_t flush_every);

    // Set severity level for immediate flush (default: error and above)
    void SetFlushOnLevel(SeverityLevel flush_on_level);

    // Dynamically set log output level
    /**
    *    日志消息流向：LOG(debug) → Logger → Sink → 输出
    *         ↓          ↓       ↓
    *   级别检查：    应用级别   Logger级别  Sink级别  该接口优先级低于 SetLogConsoleLevel(Sink)  SetLogFileLevel(Sink)
    **/
    void SetLogLevel(SeverityLevel level);

    // Set log output pattern
    void SetLogPattern(const std::string &pattern);

    // Set console log output level
    void SetLogConsoleLevel(SeverityLevel level);

    // Set file log output level
    void SetLogFileLevel(SeverityLevel level);

    // Set log buffer size (effective only in async mode, default: 8KB)
    void SetLogBufferSize(size_t size);

    // Get singleton instance
    static Logger *GetInstance();

    LogStreamOptimized GetLogStream(SeverityLevel level, const char *file, uint32_t line,
                                    const char *func);
    bool ShouldLog(SeverityLevel level) const;

private:
    void Log(SeverityLevel level, std::string &&msg, const char *file, uint32_t line,
             const char *func);
    class LoggerImpl;
    std::unique_ptr<LoggerImpl> impl_;
    Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    friend class LogStreamOptimized;
};

class LogStreamOptimized
{
public:
    LogStreamOptimized(Logger &logger, Logger::SeverityLevel level, const char *file, uint32_t line,
                       const char *func)
        : logger_(logger), level_(level), file_(file), func_(func), line_(line)
    {
    }

    ~LogStreamOptimized()
    {
        auto str = stream_.str();
        if (!str.empty()) {
            logger_.Log(level_, std::move(str), file_, line_, func_);
        }
    }

    std::ostringstream &stream() { return stream_; }

private:
    Logger &logger_;
    Logger::SeverityLevel level_;
    const char *file_;
    const char *func_;
    uint32_t line_;
    std::ostringstream stream_;
};

class LogRateLimiter
{
public:
    static bool shouldLog(const std::string &key, uint32_t interval_ms)
    {
        using namespace std::chrono;
        thread_local static std::unordered_map<std::string, steady_clock::time_point>
            last_log_times;

        auto now = steady_clock::now();
        auto it = last_log_times.find(key);
        if (it == last_log_times.end()
            || duration_cast<milliseconds>(now - it->second).count() >= interval_ms) {
            last_log_times[key] = now;
            return true;
        }
        return false;
    }
};


/*该宏定义为流，性能较原生稍有下降*/
#define LOG(level)                                                                                 \
    if (Logger::GetInstance()->ShouldLog(Logger::level))                                           \
    Logger::GetInstance()->GetLogStream(Logger::level, __FILE__, __LINE__, __FUNCTION__).stream()

#define LOG_TIME(level, interval_ms)                                                               \
    if (LogRateLimiter::shouldLog(std::string(__FILE__) + ":" + std::to_string(__LINE__),          \
                                  interval_ms))                                                    \
    LOG(level)


//TODO
// LOGF(level, fmt, ...)

#endif // LOGGER_H