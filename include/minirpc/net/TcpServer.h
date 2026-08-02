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
// 与业务解耦：只暴露 setMessageCallback/setConnectionCallback，
// 具体协议（echo、RPC）由上层回调决定。
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

  void start();  // 绑定监听地址、listen、注册 acceptChannel 读事件

 private:
  void handleNewConnection();               // accept 循环（loop 线程内）
  void removeConnection(const TcpConnectionPtr& conn);  // 跨线程安全移除
  void removeConnectionInLoop(const TcpConnectionPtr& conn);  // 实际移除（loop 线程内）

  EventLoop* loop_;                // 所属事件循环（单 reactor）
  std::string name_;               // 服务名（用于连接命名）
  InetAddress listenAddr_;         // 监听地址
  Socket listenSocket_;            // 监听 socket（RAII）
  std::unique_ptr<Channel> acceptChannel_;  // 监听 fd 的事件通道
  bool started_ = false;           // 防止重复 start
  int nextConnId_ = 1;             // 连接编号
  std::map<std::string, TcpConnectionPtr> connections_;  // 连接表：shared_ptr 保活
  MessageCallback messageCallback_;      // 收到完整数据时的回调
  ConnectionCallback connectionCallback_;  // 新连接建立回调（可选）
};

}  // namespace minirpc
