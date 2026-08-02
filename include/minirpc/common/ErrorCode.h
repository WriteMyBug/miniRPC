#pragma once

namespace minirpc {

enum class ErrorCode {
  kOk = 0,            // 成功
  kSystem = 1,        // 系统调用错误（errno）
  kInvalidArgument = 2,  // 参数/报文错误
  kTimeout = 3,       // 超时
  kClosed = 4,        // 连接已关闭
  kNoService = 5,     // 服务不存在
  kNoMethod = 6,      // 方法不存在
  kUnknown = 99,      // 未知错误
};

const char* errorCodeToString(ErrorCode code);

}  // namespace minirpc
