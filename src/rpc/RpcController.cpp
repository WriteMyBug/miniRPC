#include "minirpc/rpc/RpcController.h"

namespace minirpc {

void RpcController::Reset() {
  failed_ = false;
  canceled_ = false;
  errorCode_ = ErrorCode::kOk;
  errorText_.clear();
  cancelCallback_ = nullptr;
}

void RpcController::StartCancel() {
  canceled_ = true;
  if (cancelCallback_ != nullptr) {
    google::protobuf::Closure* cb = cancelCallback_;
    cancelCallback_ = nullptr;
    cb->Run();
  }
}

void RpcController::SetFailed(const std::string& reason) {
  failed_ = true;
  errorText_ = reason;
  if (errorCode_ == ErrorCode::kOk) {
    errorCode_ = ErrorCode::kUnknown;
  }
}

void RpcController::SetFailed(ErrorCode code, const std::string& reason) {
  failed_ = true;
  errorCode_ = code;
  errorText_ = reason;
}

void RpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
  if (callback == nullptr) {
    return;
  }
  if (canceled_.load()) {
    callback->Run();
  } else {
    cancelCallback_ = callback;
  }
}

}  // namespace minirpc

