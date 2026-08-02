#include "minirpc/rpc/LoadBalanceChannel.h"

#include <cstdlib>
#include <utility>

#include "minirpc/common/Logger.h"
#include "minirpc/rpc/RpcChannel.h"
#include "minirpc/rpc/RpcController.h"

namespace minirpc {

LoadBalanceChannel::LoadBalanceChannel(const InetAddress& registryAddr,
                                       std::string serviceName,
                                       BalanceType type,
                                       int refreshIntervalMs,
                                       int callTimeoutMs, int callRetries)
    : registryAddr_(registryAddr),
      serviceName_(std::move(serviceName)),
      type_(type),
      refreshIntervalMs_(refreshIntervalMs),
      callTimeoutMs_(callTimeoutMs),
      callRetries_(callRetries),
      registryChannel_(
          std::make_unique<minirpc::RpcChannel>(registryAddr_, 1500, 1)),
      registryStub_(
          std::make_unique<minirpc::registry::RegistryService::Stub>(
              registryChannel_.get())) {}

LoadBalanceChannel::~LoadBalanceChannel() = default;

std::string LoadBalanceChannel::nodeKeyOf(
    const minirpc::registry::NodeInfo& node) {
  return node.host() + ":" + std::to_string(node.port());
}

bool LoadBalanceChannel::needRefresh() const {
  return nodes_.empty() ||
         std::chrono::steady_clock::now() - lastRefresh_ >=
             std::chrono::milliseconds(refreshIntervalMs_);
}

bool LoadBalanceChannel::refreshNodes() {
  // 通过注册中心 Discover 拉取存活节点，重建轮询计数与一致性哈希环。
  RpcController ctrl;
  minirpc::registry::DiscoverRequest req;
  req.set_service_name(serviceName_);
  minirpc::registry::DiscoverResponse resp;
  registryStub_->Discover(&ctrl, &req, &resp, nullptr);
  if (ctrl.Failed()) {
    LOG_WARN << "LoadBalanceChannel: discover failed: " << ctrl.ErrorText();
    return false;
  }
  std::vector<minirpc::registry::NodeInfo> nodes;
  for (const auto& n : resp.nodes()) {
    nodes.push_back(n);
  }
  nodes_ = std::move(nodes);
  std::vector<std::string> keys;
  keys.reserve(nodes_.size());
  for (const auto& n : nodes_) {
    keys.push_back(nodeKeyOf(n));
  }
  roundRobin_.setNodeCount(nodes_.size());
  consistentHash_.setNodes(keys);
  lastRefresh_ = std::chrono::steady_clock::now();  // 记录刷新时间，供 TTL 判断
  LOG_INFO << "LoadBalanceChannel: service " << serviceName_ << " has "
           << nodes_.size() << " node(s)";
  return true;
}

std::string LoadBalanceChannel::selectNodeKey(const std::string& hashKey) {
  if (nodes_.empty()) {
    return {};
  }
  if (type_ == BalanceType::kConsistentHash && !hashKey.empty()) {
    // 一致性哈希：用请求内容当 key，同一请求稳定落在同一节点。
    const std::string key = consistentHash_.selectNode(hashKey);
    if (!key.empty()) {
      return key;
    }
  }
  return nodeKeyOf(nodes_[roundRobin_.next() % nodes_.size()]);  // 轮询取模
}

std::string LoadBalanceChannel::selectNodeKeyForRetry(
    const std::string& failedKey) {
  if (nodes_.empty()) {
    return {};
  }
  for (size_t i = 0; i < nodes_.size(); ++i) {
    const std::string key =
        nodeKeyOf(nodes_[roundRobin_.next() % nodes_.size()]);
    if (key != failedKey) {
      return key;
    }
  }
  return failedKey;
}

minirpc::RpcChannel* LoadBalanceChannel::channelFor(
    const std::string& nodeKey) {
  const auto it = channels_.find(nodeKey);
  if (it != channels_.end()) {
    return it->second.get();  // 每个节点复用一条连接，避免频繁建连
  }
  const size_t colon = nodeKey.rfind(':');
  const std::string host = nodeKey.substr(0, colon);
  const uint16_t port =
      static_cast<uint16_t>(std::atoi(nodeKey.c_str() + colon + 1));
  auto channel = std::make_unique<minirpc::RpcChannel>(
      InetAddress(host, port), callTimeoutMs_, callRetries_);
  minirpc::RpcChannel* raw = channel.get();
  channels_[nodeKey] = std::move(channel);
  return raw;
}

void LoadBalanceChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message* request,
    google::protobuf::Message* response, google::protobuf::Closure* done) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (controller != nullptr) {
    controller->Reset();
  }
  if (needRefresh()) {
    refreshNodes();
  }
  if (nodes_.empty()) {
    // 注册中心没有可用节点：直接报 kNoService，不进入调用流程。
    failController(controller, ErrorCode::kNoService,
                   "no available node for service: " + serviceName_);
    if (done != nullptr) {
      done->Run();
    }
    return;
  }

  std::string hashKey;
  if (type_ == BalanceType::kConsistentHash) {
    request->SerializeToString(&hashKey);
  }
  // 异步（带 done）场景不做自动重试，避免回调被多次调用。
  const int maxAttempts = (done != nullptr) ? 1 : 2;
  for (int attempt = 0; attempt < maxAttempts; ++attempt) {
    const std::string nodeKey =
        (attempt == 0) ? selectNodeKey(hashKey)
                       : selectNodeKeyForRetry(failedNodeKey_);
    minirpc::RpcChannel* channel = channelFor(nodeKey);
    channel->CallMethod(method, controller, request, response, done);
    if (controller == nullptr || !controller->Failed()) {
      return;  // 调用成功
    }
    auto* rc = dynamic_cast<RpcController*>(controller);
    const ErrorCode code = rc ? rc->errorCode() : ErrorCode::kUnknown;
    if (code != ErrorCode::kClosed && code != ErrorCode::kSystem &&
        code != ErrorCode::kTimeout) {
      return;  // 服务端业务错误，不重试
    }
    if (nodes_.size() <= 1) {
      return;
    }
    failedNodeKey_ = nodeKey;
    // 连接级失败：节点可能已下线，先刷新列表再换一个节点重试。
    refreshNodes();  // 节点可能已下线，刷新后再换节点
    if (nodes_.size() <= 1) {
      return;
    }
  }
}

size_t LoadBalanceChannel::nodeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_.size();
}

void LoadBalanceChannel::failController(
    google::protobuf::RpcController* controller, ErrorCode code,
    const std::string& message) {
  if (controller == nullptr) {
    return;
  }
  if (auto* rc = dynamic_cast<RpcController*>(controller)) {
    rc->SetFailed(code, message);
  } else {
    controller->SetFailed(message);
  }
}

}  // namespace minirpc
