#pragma once

#include <cstdint>
#include <string>

namespace isdm {

/**
 * @brief 状态码类，用于错误处理
 *
 * 采用类似Google的Status模式，避免使用异常。
 *
 * @code
 * Status status = DoSomething();
 * if (!status.ok()) {
 *   LOG(ERROR) << "Failed: " << status.message();
 *   return;
 * }
 * @endcode
 */
class Status {
 public:
  /// 创建成功状态
  static Status OK() { return Status(); }

  /// 创建错误状态
  static Status Error(std::string message) {
    return Status(std::move(message));
  }

  /// 检查是否成功
  bool ok() const { return error_message_.empty(); }

  /// 获取错误消息
  const std::string& message() const { return error_message_; }

  /// 隐式转换为bool（方便if判断）
  explicit operator bool() const { return ok(); }

 private:
  Status() = default;
  explicit Status(std::string msg) : error_message_(std::move(msg)) {}

  std::string error_message_;
};

} // namespace isdm
