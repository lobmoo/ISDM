#include "logger.h"
#include "logger_impl.h"

Logger::Logger() : impl_(std::make_unique<LoggerImpl>())
{
}

Logger *Logger::GetInstance()
{
    static Logger *instance = new Logger();
    return instance;
}

void Logger::Uninit()
{
    return impl_->Uninit();
}

bool Logger::Init(const std::string &log_config_file_path)
{
    return impl_->Init(log_config_file_path);
}

bool Logger::Init(const std::string &file_name, LoggerType type, SeverityLevel level,
                  uint32_t max_file_size, uint32_t max_backup_index, bool is_async)
{
    return impl_->Init(file_name, type, level, max_file_size, max_backup_index, is_async);
}

LogStreamOptimized Logger::GetLogStream(SeverityLevel level, const char *file, uint32_t line,
                                        const char *func)
{
    return LogStreamOptimized(*this, level, file, line, func);
}

void Logger::Log(SeverityLevel level, std::string &&msg, const char *file, uint32_t line,
                 const char *func)
{
    impl_->Log(level, std::move(msg), file, line, func);
}

bool Logger::ShouldLog(SeverityLevel level) const
{
    return impl_ && impl_->ShouldLog(level);
}

void Logger::SetFlushEvery(uint32_t flush_every)
{
    impl_->SetFlushEvery(flush_every);
}

void Logger::SetFlushOnLevel(Logger::SeverityLevel flush_on_level)
{
    impl_->SetFlushOnLevel(flush_on_level);
}

void Logger::SetLogLevel(Logger::SeverityLevel level)
{
    impl_->SetLogLevel(level);
}

void Logger::SetLogPattern(const std::string &pattern)
{
    impl_->SetLogPattern(pattern);
}

void Logger::SetLogConsoleLevel(Logger::SeverityLevel level)
{
    impl_->SetLogConsoleLevel(level);
}

void Logger::SetLogFileLevel(Logger::SeverityLevel level)
{
    impl_->SetLogFileLevel(level);
}

void Logger::SetLogBufferSize(size_t size)
{
    impl_->SetLogBufferSize(size);
}