#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <google/protobuf/service.h>

#include "minirpc/common/ErrorCode.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/LoadBalancer.h"
#include "registry.pb.h"

namespace minirpc {

class RpcChannel;

// 负载均衡 RPC 通道：从注册中心发现节点，按策略（轮询/一致性哈希）选节点同步调用。
class LoadBalanceChannel : public google::protobuf::RpcChannel {
 public:
  enum class BalanceType { kRoundRobin, kConsistentHash };

  LoadBalanceChannel(const InetAddress& registryAddr, std::string serviceName,
                     BalanceType type = BalanceType::kRoundRobin,
                     int refreshIntervalMs = 3000, int callTimeoutMs = 2000,
                     int callRetries = 1);
  ~LoadBalanceChannel() override;

  void CallMethod(const google::protobuf::MethodDescriptor* method,
                  google::protobuf::RpcController* controller,
                  const google::protobuf::Message* request,
                  google::protobuf::Message* response,
                  google::protobuf::Closure* done) override;

  size_t nodeCount() const;

 private:
  bool needRefresh() const;
  bool refreshNodes();
  std::string selectNodeKey(const std::string& hashKey);
  std::string selectNodeKeyForRetry(const std::string& failedKey);
  minirpc::RpcChannel* channelFor(const std::string& nodeKey);
  static std::string nodeKeyOf(const minirpc::registry::NodeInfo& node);
  static void failController(google::protobuf::RpcController* controller,
                             ErrorCode code, const std::string& message);

  InetAddress registryAddr_;
  std::string serviceName_;
  BalanceType type_;
  int refreshIntervalMs_;
  int callTimeoutMs_;
  int callRetries_;
  mutable std::mutex mutex_;
  std::chrono::steady_clock::time_point lastRefresh_{};
  std::vector<minirpc::registry::NodeInfo> nodes_;
  std::unordered_map<std::string, std::unique_ptr<minirpc::RpcChannel>> channels_;
  std::unique_ptr<minirpc::RpcChannel> registryChannel_;
  std::unique_ptr<minirpc::registry::RegistryService::Stub> registryStub_;
  RoundRobin roundRobin_;
  ConsistentHash consistentHash_;
  std::string failedNodeKey_;
};

}  // namespace minirpc
