#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <google/protobuf/service.h>

#include "registry.pb.h"

namespace minirpc {

// 注册中心服务实现：服务注册/发现/心跳剔除（基于我们自己的 RPC 框架承载）。
class RegistryServiceImpl : public minirpc::registry::RegistryService {
 public:
  explicit RegistryServiceImpl(
      std::chrono::milliseconds heartbeatTimeout = std::chrono::milliseconds(6000));
  ~RegistryServiceImpl() override;

  void Register(google::protobuf::RpcController* controller,
                const minirpc::registry::RegisterRequest* request,
                minirpc::registry::RegisterResponse* response,
                google::protobuf::Closure* done) override;
  void Unregister(google::protobuf::RpcController* controller,
                  const minirpc::registry::UnregisterRequest* request,
                  minirpc::registry::UnregisterResponse* response,
                  google::protobuf::Closure* done) override;
  void Discover(google::protobuf::RpcController* controller,
                const minirpc::registry::DiscoverRequest* request,
                minirpc::registry::DiscoverResponse* response,
                google::protobuf::Closure* done) override;
  void Heartbeat(google::protobuf::RpcController* controller,
                 const minirpc::registry::HeartbeatRequest* request,
                 minirpc::registry::HeartbeatResponse* response,
                 google::protobuf::Closure* done) override;

  size_t liveNodeCount(const std::string& serviceName) const;

 private:
  struct NodeEntry {
    minirpc::registry::NodeInfo node;
    std::chrono::steady_clock::time_point lastHeartbeat;
  };
  using ServiceMap = std::unordered_map<std::string, NodeEntry>;

  static std::string nodeKey(const minirpc::registry::NodeInfo& node);
  void purgeExpired();
  void evictionLoop();

  mutable std::mutex mutex_;
  std::unordered_map<std::string, ServiceMap> services_;
  std::chrono::milliseconds heartbeatTimeout_;
  std::atomic<bool> stopEviction_{false};
  std::thread evictionThread_;
};

}  // namespace minirpc

