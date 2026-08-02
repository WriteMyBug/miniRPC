#pragma once

#include <atomic>
#include <string>

#include <google/protobuf/service.h>

#include "minirpc/common/ErrorCode.h"

namespace minirpc {

// 实现 google::protobuf::RpcController：错误码 + 错误文本 + 取消标记。
class RpcController : public google::protobuf::RpcController {
 public:
  RpcController() = default;

  void Reset() override;
  bool Failed() const override { return failed_.load(); }
  std::string ErrorText() const override { return errorText_; }
  void StartCancel() override;
  void SetFailed(const std::string& reason) override;
  bool IsCanceled() const override { return canceled_.load(); }
  void NotifyOnCancel(google::protobuf::Closure* callback) override;

  // minirpc 扩展：带错误码失败
  void SetFailed(ErrorCode code, const std::string& reason);
  ErrorCode errorCode() const { return errorCode_; }

 private:
  std::atomic<bool> failed_{false};
  std::atomic<bool> canceled_{false};
  ErrorCode errorCode_{ErrorCode::kOk};
  std::string errorText_;
  google::protobuf::Closure* cancelCallback_ = nullptr;
};

}  // namespace minirpc

