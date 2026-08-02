#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include <google/protobuf/service.h>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

// 服务注册表：按 Service 完整名（package.Service）查找 protobuf Service 实例。
class ServiceRegistry : public Noncopyable {
 public:
  bool addService(const std::string& fullName, google::protobuf::Service* service);
  google::protobuf::Service* findService(const std::string& fullName) const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, google::protobuf::Service*> services_;
};

}  // namespace minirpc

