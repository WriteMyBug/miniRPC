#include "minirpc/common/ErrorCode.h"

namespace minirpc {

const char* errorCodeToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::kOk:             return "OK";
    case ErrorCode::kSystem:         return "SYSTEM_ERROR";
    case ErrorCode::kInvalidArgument: return "INVALID_ARGUMENT";
    case ErrorCode::kTimeout:        return "TIMEOUT";
    case ErrorCode::kClosed:         return "CONNECTION_CLOSED";
    case ErrorCode::kNoService:      return "NO_SERVICE";
    case ErrorCode::kNoMethod:       return "NO_METHOD";
    case ErrorCode::kUnknown:        return "UNKNOWN";
  }
  return "UNKNOWN";
}

}  // namespace minirpc

