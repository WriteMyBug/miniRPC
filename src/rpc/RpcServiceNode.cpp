#include "minirpc/rpc/RpcServiceNode.h"

#include <chrono>
#include <utility>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/rpc/RpcChannel.h"
#include "minirpc/rpc/RpcController.h"
#include "registry.pb.h"

namespace minirpc {

RpcServiceNode::RpcServiceNode(EventLoop* loop, const InetAddress& listenAddr,
                               std::string nodeName,
                               const InetAddress& registryAddr,
                               std::string serviceName,
                               int heartbeatIntervalMs, int heartbeatTimeoutMs)
    : loop_(loop),
      listenAddr_(listenAddr),
      nodeName_(std::move(nodeName)),
      registryAddr_(registryAddr),
      serviceName_(std::move(serviceName)),
      heartbeatIntervalMs_(heartbeatIntervalMs),
      heartbeatTimeoutMs_(heartbeatTimeoutMs),
      server_(loop, listenAddr_, nodeName_) {}

RpcServiceNode::~RpcServiceNode() {
  stopHeartbeat();
}

void RpcServiceNode::registerService(google::protobuf::Service* service) {
  server_.registerService(service);
}

void RpcServiceNode::start() {
  server_.start();
  registerSelf();
  heartbeatThread_ = std::thread([this] { heartbeatLoop(); });
  LOG_INFO << "RpcServiceNode[" << nodeName_ << "] started, heartbeat every "
           << heartbeatIntervalMs_ << "ms";
}

void RpcServiceNode::stopHeartbeat() {
  stop_ = true;
  cv_.notify_all();
  if (heartbeatThread_.joinable()) {
    heartbeatThread_.join();
  }
}

void RpcServiceNode::shutdown() {
  stopHeartbeat();
  unregisterSelf();
}

void RpcServiceNode::registerSelf() {
  RpcChannel channel(registryAddr_, 1000, 1);
  minirpc::registry::RegistryService::Stub stub(&channel);
  RpcController ctrl;
  minirpc::registry::RegisterRequest req;
  req.set_service_name(serviceName_);
  req.mutable_node()->set_name(nodeName_);
  req.mutable_node()->set_host("127.0.0.1");
  req.mutable_node()->set_port(listenAddr_.port());
  minirpc::registry::RegisterResponse resp;
  stub.Register(&ctrl, &req, &resp, nullptr);
  if (ctrl.Failed()) {
    LOG_ERROR << "RpcServiceNode[" << nodeName_
              << "] register failed: " << ctrl.ErrorText();
  } else {
    LOG_INFO << "RpcServiceNode[" << nodeName_ << "] registered to registry";
  }
}

void RpcServiceNode::unregisterSelf() {
  RpcChannel channel(registryAddr_, 1000, 1);
  minirpc::registry::RegistryService::Stub stub(&channel);
  RpcController ctrl;
  minirpc::registry::UnregisterRequest req;
  req.set_service_name(serviceName_);
  req.mutable_node()->set_name(nodeName_);
  req.mutable_node()->set_host("127.0.0.1");
  req.mutable_node()->set_port(listenAddr_.port());
  minirpc::registry::UnregisterResponse resp;
  stub.Unregister(&ctrl, &req, &resp, nullptr);
  LOG_INFO << "RpcServiceNode[" << nodeName_ << "] unregistered (best effort)";
}

void RpcServiceNode::heartbeatLoop() {
  RpcChannel channel(registryAddr_, heartbeatTimeoutMs_ / 2 + 200, 1);
  minirpc::registry::RegistryService::Stub stub(&channel);
  while (!stop_) {
    {
      RpcController ctrl;
      minirpc::registry::HeartbeatRequest req;
      req.set_service_name(serviceName_);
      req.mutable_node()->set_name(nodeName_);
      req.mutable_node()->set_host("127.0.0.1");
      req.mutable_node()->set_port(listenAddr_.port());
      minirpc::registry::HeartbeatResponse resp;
      stub.Heartbeat(&ctrl, &req, &resp, nullptr);
      if (ctrl.Failed()) {
        LOG_WARN << "RpcServiceNode[" << nodeName_
                 << "] heartbeat failed: " << ctrl.ErrorText();
      }
    }
    std::unique_lock<std::mutex> lock(cvMutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(heartbeatIntervalMs_),
                 [this] { return stop_.load(); });
  }
}

}  // namespace minirpc

