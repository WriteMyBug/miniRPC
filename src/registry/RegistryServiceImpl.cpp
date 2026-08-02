#include "minirpc/registry/RegistryServiceImpl.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <utility>
#include <vector>

#include "minirpc/common/ErrorCode.h"
#include "minirpc/common/Logger.h"

namespace minirpc {

namespace {

constexpr auto kEvictionIntervalMs = std::chrono::milliseconds(500);

int32_t okCode() {
  return static_cast<int32_t>(ErrorCode::kOk);
}

int32_t errCode() {
  return static_cast<int32_t>(ErrorCode::kUnknown);
}

}  // namespace

RegistryServiceImpl::RegistryServiceImpl(
    std::chrono::milliseconds heartbeatTimeout)
    : heartbeatTimeout_(heartbeatTimeout) {
  evictionThread_ = std::thread([this] { evictionLoop(); });
}

RegistryServiceImpl::~RegistryServiceImpl() {
  stopEviction_ = true;
  if (evictionThread_.joinable()) {
    evictionThread_.join();
  }
}

std::string RegistryServiceImpl::nodeKey(
    const minirpc::registry::NodeInfo& node) {
  return node.host() + ":" + std::to_string(node.port());
}

void RegistryServiceImpl::purgeExpired() {
  const auto now = std::chrono::steady_clock::now();
  for (auto svcIt = services_.begin(); svcIt != services_.end();) {
    auto& nodes = svcIt->second;
    for (auto it = nodes.begin(); it != nodes.end();) {
      // 心跳超时 = 判定节点"死亡"，从注册表删除（故障剔除核心）。
      if (now - it->second.lastHeartbeat > heartbeatTimeout_) {
        LOG_WARN << "Registry: evict stale node "
                 << svcIt->first << "@" << it->first;
        it = nodes.erase(it);
      } else {
        ++it;
      }
    }
    if (nodes.empty()) {
      svcIt = services_.erase(svcIt);
    } else {
      ++svcIt;
    }
  }
}

void RegistryServiceImpl::evictionLoop() {
  // 后台剔除线程：每 500ms 扫一次过期节点，与请求处理互不阻塞。
  while (!stopEviction_) {
    std::this_thread::sleep_for(kEvictionIntervalMs);
    if (stopEviction_) {
      break;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpired();
  }
}

void RegistryServiceImpl::Register(
    google::protobuf::RpcController* controller,
    const minirpc::registry::RegisterRequest* request,
    minirpc::registry::RegisterResponse* response,
    google::protobuf::Closure* done) {
  (void)controller;
  response->set_error_code(okCode());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // 注册 = 写入/覆盖该服务下的节点，并记下当前时间作为首次心跳。
    NodeEntry entry{request->node(), std::chrono::steady_clock::now()};
    services_[request->service_name()][nodeKey(request->node())] =
        std::move(entry);
  }
  LOG_INFO << "Registry: registered " << request->service_name() << "@"
           << nodeKey(request->node());
  if (done != nullptr) {
    done->Run();
  }
}

void RegistryServiceImpl::Unregister(
    google::protobuf::RpcController* controller,
    const minirpc::registry::UnregisterRequest* request,
    minirpc::registry::UnregisterResponse* response,
    google::protobuf::Closure* done) {
  (void)controller;
  response->set_error_code(okCode());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto svcIt = services_.find(request->service_name());
    if (svcIt != services_.end()) {
      svcIt->second.erase(nodeKey(request->node()));
      if (svcIt->second.empty()) {
        services_.erase(svcIt);
      }
    }
  }
  LOG_INFO << "Registry: unregistered " << request->service_name() << "@"
           << nodeKey(request->node());
  if (done != nullptr) {
    done->Run();
  }
}

void RegistryServiceImpl::Discover(
    google::protobuf::RpcController* controller,
    const minirpc::registry::DiscoverRequest* request,
    minirpc::registry::DiscoverResponse* response,
    google::protobuf::Closure* done) {
  (void)controller;
  response->set_error_code(okCode());
  std::vector<minirpc::registry::NodeInfo> nodes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpired();
    const auto svcIt = services_.find(request->service_name());
    if (svcIt != services_.end()) {
      for (const auto& kv : svcIt->second) {
        nodes.push_back(kv.second.node);
      }
    }
  }
  std::sort(nodes.begin(), nodes.end(),
            [](const minirpc::registry::NodeInfo& a,
               const minirpc::registry::NodeInfo& b) {
              return std::make_pair(a.host(), a.port()) <
                     std::make_pair(b.host(), b.port());
            });
  for (const auto& n : nodes) {
    *response->add_nodes() = n;
  }
  if (done != nullptr) {
    done->Run();
  }
}

void RegistryServiceImpl::Heartbeat(
    google::protobuf::RpcController* controller,
    const minirpc::registry::HeartbeatRequest* request,
    minirpc::registry::HeartbeatResponse* response,
    google::protobuf::Closure* done) {
  (void)controller;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto svcIt = services_.find(request->service_name());
  const std::string key = nodeKey(request->node());
  if (svcIt == services_.end() || svcIt->second.count(key) == 0) {
    // 没注册过就心跳：拒绝（节点必须先 Register）。
    response->set_error_code(errCode());
    response->set_error_message("node not registered");
  } else {
    // 心跳只刷新时间戳，不重复写入整条节点信息。
    svcIt->second.at(key).lastHeartbeat = std::chrono::steady_clock::now();
    response->set_error_code(okCode());
  }
  if (done != nullptr) {
    done->Run();
  }
}

size_t RegistryServiceImpl::liveNodeCount(
    const std::string& serviceName) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto svcIt = services_.find(serviceName);
  if (svcIt == services_.end()) {
    return 0;
  }
  size_t live = 0;
  const auto now = std::chrono::steady_clock::now();
  for (const auto& kv : svcIt->second) {
    if (now - kv.second.lastHeartbeat <= heartbeatTimeout_) {
      ++live;
    }
  }
  return live;
}

}  // namespace minirpc
