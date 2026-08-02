#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <google/protobuf/service.h>

#include "minirpc/common/Noncopyable.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/RpcServer.h"

namespace minirpc {

class EventLoop;

// 服务节点：包装 RpcServer，启动时向注册中心注册，并周期性发送心跳。
class RpcServiceNode : public Noncopyable {
 public:
  RpcServiceNode(EventLoop* loop, const InetAddress& listenAddr,
                 std::string nodeName, const InetAddress& registryAddr,
                 std::string serviceName, int heartbeatIntervalMs = 2000,
                 int heartbeatTimeoutMs = 6000);
  ~RpcServiceNode();

  void registerService(google::protobuf::Service* service);
  void start();          // 注册 + 启动心跳线程
  void stopHeartbeat();  // 停止心跳但不注销（模拟故障/崩溃）
  void shutdown();       // 停止心跳并注销（优雅下线）

  const std::string& serviceName() const { return serviceName_; }
  uint16_t port() const { return listenAddr_.port(); }

 private:
  void heartbeatLoop();
  void registerSelf();
  void unregisterSelf();

  EventLoop* loop_;
  InetAddress listenAddr_;
  std::string nodeName_;
  InetAddress registryAddr_;
  std::string serviceName_;
  int heartbeatIntervalMs_;
  int heartbeatTimeoutMs_;
  RpcServer server_;
  std::thread heartbeatThread_;
  std::atomic<bool> stop_{false};
  std::mutex cvMutex_;
  std::condition_variable cv_;
};

}  // namespace minirpc

