#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "minirpc/common/Noncopyable.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/net/Socket.h"
#include "minirpc/net/TcpConnection.h"

namespace minirpc {

class Channel;
class EventLoop;

// TCP 服务端：监听 accept + 连接管理 + 消息分发（单事件循环）。
class TcpServer : public Noncopyable {
 public:
  using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
  using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;

  TcpServer(EventLoop* loop, const InetAddress& listenAddr, std::string name);
  ~TcpServer();

  void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
  void setConnectionCallback(ConnectionCallback cb) {
    connectionCallback_ = std::move(cb);
  }

  void start();

 private:
  void handleNewConnection();
  void removeConnection(const TcpConnectionPtr& conn);
  void removeConnectionInLoop(const TcpConnectionPtr& conn);

  EventLoop* loop_;
  std::string name_;
  InetAddress listenAddr_;
  Socket listenSocket_;
  std::unique_ptr<Channel> acceptChannel_;
  bool started_ = false;
  int nextConnId_ = 1;
  std::map<std::string, TcpConnectionPtr> connections_;
  MessageCallback messageCallback_;
  ConnectionCallback connectionCallback_;
};

}  // namespace minirpc

