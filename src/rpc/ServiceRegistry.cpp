#include "minirpc/rpc/ServiceRegistry.h"

#include <utility>

namespace minirpc {

bool ServiceRegistry::addService(const std::string& fullName,
                                 google::protobuf::Service* service) {
  std::lock_guard<std::mutex> lock(mutex_);
  // 同名服务重复注册直接拒绝，避免歧义分发。
  if (services_.count(fullName) > 0) {
    return false;
  }
  services_[fullName] = service;
  return true;
}

google::protobuf::Service* ServiceRegistry::findService(
    const std::string& fullName) const {
  std::lock_guard<std::mutex> lock(mutex_);
  // 找不到返回 nullptr，调用方（RpcServer）回 kNoService 错误。
  const auto it = services_.find(fullName);
  return it == services_.end() ? nullptr : it->second;
}

}  // namespace minirpc
