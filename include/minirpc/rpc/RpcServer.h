#pragma once

#include <memory>
#include <string>

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

#include "minirpc/codec/Protocol.h"
#include "minirpc/common/ErrorCode.h"
#include "minirpc/common/Noncopyable.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/net/TcpConnection.h"
#include "minirpc/net/TcpServer.h"
#include "minirpc/rpc/ServiceRegistry.h"

namespace minirpc {

class EventLoop;
class EventLoopPool;
class ThreadPool;

// RPC 服务端：epoll(TcpServer) 收包 -> Codec 拆包 -> 线程池分发 -> Service 调用。
// ioThreads > 0 时启用多 Reactor：内部创建 EventLoopPool，连接 IO 分发到多个
// 子事件循环（多核利用）；ioThreads == 0 保持单 Reactor 行为。
class RpcServer : public Noncopyable {
 public:
  RpcServer(EventLoop* loop, const InetAddress& listenAddr, std::string name,
            size_t ioThreads = 0);
  ~RpcServer();

  void registerService(google::protobuf::Service* service);
  void setThreadPoolSize(size_t n);
  void start();

 private:
  void onMessage(const TcpConnectionPtr& conn, Buffer* buffer);
  void handleRequest(const TcpConnectionPtr& conn, ProtocolMessage requestMsg);
  void sendError(const TcpConnectionPtr& conn, uint64_t seq, ErrorCode code,
                 const std::string& message);
  void sendEnvelope(const TcpConnectionPtr& conn, uint64_t seq,
                    const google::protobuf::Message& envelope);

  EventLoop* loop_;
  std::unique_ptr<EventLoopPool> ioPool_;  // 多 Reactor 池（ioThreads>0 时创建）
  TcpServer server_;
  ServiceRegistry registry_;
  std::unique_ptr<ThreadPool> pool_;
  size_t poolThreads_ = 4;
};

}  // namespace minirpc
