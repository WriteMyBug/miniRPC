#pragma once

namespace minirpc {

enum class ErrorCode {
  kOk = 0,
  kSystem,          // 系统调用错误（errno）
  kInvalidArgument, // 参数错误
  kTimeout,         // 超时
  kClosed,          // 连接已关闭
  kUnknown,
};

}  // namespace minirpc

