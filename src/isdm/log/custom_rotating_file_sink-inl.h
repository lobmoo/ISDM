#pragma once

#ifndef SPDLOG_HEADER_ONLY
#    include <custom_rotating_file_sink.h>
#endif

#include <spdlog/common.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/fmt/fmt.h>

#include <cerrno>
#include <chrono>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include <algorithm>
#include <filesystem>

#ifndef _WIN32
#include <climits>
#include <unistd.h>
#endif

namespace spdlog
{
namespace sinks
{

    template <typename Mutex>
    SPDLOG_INLINE custom_rotating_file_sink<Mutex>::custom_rotating_file_sink(
        filename_t base_filename, std::size_t max_size, std::size_t max_files, bool rotate_on_open,
        const file_event_handlers &event_handlers)
        : base_filename_(std::move(base_filename)), max_size_(max_size),
          max_files_(max_files), file_helper_{event_handlers}
    {
        if (max_size == 0) {
            throw_spdlog_ex("rotating sink constructor: max_size arg cannot be zero");
        }

        if (max_files > 200000) {
            throw_spdlog_ex("rotating sink constructor: max_files arg cannot exceed 200000");
        }
        file_helper_.open(base_filename_);
        current_size_ = file_helper_.size(); // expensive. called only once
        if (rotate_on_open && current_size_ > 0) {
            rotate_();
            current_size_ = 0;
        }
    }

    inline std::string get_process_name()
    {
#ifdef _WIN32
        char process_path[MAX_PATH];
        if (GetModuleFileNameA(NULL, process_path, MAX_PATH)) {
            std::string fullPath(process_path);
            size_t pos = fullPath.find_last_of("\\/");
            if (pos != std::string::npos)
                return fullPath.substr(pos + 1);
            else
                return fullPath;
        }
        return "unknown";
#else
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len != -1) {
            buf[len] = '\0';
            std::string fullPath(buf);
            size_t pos = fullPath.find_last_of('/');
            if (pos != std::string::npos)
                return fullPath.substr(pos + 1);
            else
                return fullPath;
        }
        return "unknown";
#endif
    }

    inline std::string get_timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
        return ss.str();
    }

    template <typename Mutex>
    SPDLOG_INLINE filename_t custom_rotating_file_sink<Mutex>::filename()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        return file_helper_.filename();
    }

    template <typename Mutex>
    SPDLOG_INLINE void custom_rotating_file_sink<Mutex>::rotate_now()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        rotate_();
    }

    template <typename Mutex>
    SPDLOG_INLINE void custom_rotating_file_sink<Mutex>::sink_it_(const details::log_msg &msg)
    {
        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);
        auto new_size = current_size_ + formatted.size();

        if (new_size > max_size_) {
            file_helper_.flush();
            if (file_helper_.size() > 0) {
                rotate_();
                new_size = formatted.size();
            }
        }
        file_helper_.write(formatted);
        current_size_ = new_size;
    }

    template <typename Mutex>
    SPDLOG_INLINE void custom_rotating_file_sink<Mutex>::flush_()
    {
        file_helper_.flush();
    }

    template <typename Mutex>
    SPDLOG_INLINE void custom_rotating_file_sink<Mutex>::rotate_()
    {
        using details::os::filename_to_str;
        using details::os::path_exists;

        file_helper_.close();

        // Generate new filename with process name and timestamp
        std::string process_name = get_process_name();
        std::string timestamp = get_timestamp();

        // Convert to filename_t with proper encoding
        filename_t process_name_t, timestamp_t;
#ifdef _WIN32
        process_name_t = details::os::utf8_to_wstr(process_name);
        timestamp_t = details::os::utf8_to_wstr(timestamp);
#else
        process_name_t = process_name;
        timestamp_t = timestamp;
#endif

        // Split base filename into directory and filename parts
        std::filesystem::path base_path(base_filename_);
        filename_t dir = base_path.parent_path().native();
        filename_t user_prefix_t = base_path.stem().native();
        if (user_prefix_t.empty()) {
            user_prefix_t = process_name_t;
        }
        filename_t new_filename;

        if (dir.empty()) {
            new_filename =
                fmt_lib::format(SPDLOG_FILENAME_T("{}_{}.log"), user_prefix_t, timestamp_t);
        } else {
            new_filename =
                (std::filesystem::path(dir)
                 / fmt_lib::format(SPDLOG_FILENAME_T("{}_{}.log"), user_prefix_t, timestamp_t))
                    .native();
        }

        // Rename current file to new filename
        if (!rename_file_(base_filename_, new_filename)) {
            file_helper_.reopen(true);
            current_size_ = 0;
            throw_spdlog_ex("Failed to rotate file: " + filename_to_str(base_filename_) + " to "
                            + filename_to_str(new_filename));
        }

        // Collect and delete old files
        std::vector<std::filesystem::path> files;
        try {
            std::filesystem::path dir_path(dir.empty() ? std::filesystem::current_path()
                                                       : std::filesystem::path(dir));
            filename_t prefix = fmt_lib::format(SPDLOG_FILENAME_T("{}_"), user_prefix_t);

            for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
                filename_t filename = entry.path().filename().native();
                if (filename.find(prefix) == 0
                    && filename.rfind(SPDLOG_FILENAME_T(".log")) == filename.length() - 4) {
                    files.push_back(entry.path());
                }
            }

            // Sort files by creation time (oldest first)
            std::sort(files.begin(), files.end(),
                      [](const std::filesystem::path &a, const std::filesystem::path &b) {
                          return std::filesystem::last_write_time(a)
                                 < std::filesystem::last_write_time(b);
                      });

            // Delete oldest files if exceeding max_files
            while (files.size() > max_files_) {
                std::filesystem::remove(files.front());
                files.erase(files.begin());
            }

        } catch (const std::exception &ex) {
            SPDLOG_THROW(spdlog_ex("Error during file rotation: " + std::string(ex.what())));
        }

        // Reopen main log file
        file_helper_.open(base_filename_);
        current_size_ = 0;
    }

    template <typename Mutex>
    SPDLOG_INLINE bool
    custom_rotating_file_sink<Mutex>::rename_file_(const filename_t &src_filename,
                                                   const filename_t &target_filename)
    {
        return details::os::rename(src_filename, target_filename) == 0;
    }

} // namespace sinks
} // namespace spdlog