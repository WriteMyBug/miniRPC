#pragma once

#include <functional>
#include <memory>
#include <string>

#include "minirpc/common/Noncopyable.h"
#include "minirpc/net/Buffer.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/net/Socket.h"

namespace minirpc {

class Channel;
class EventLoop;
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

// 一条已建立的 TCP 连接：负责读写、收发缓冲与生命周期回调。
// 生命周期由 shared_ptr 管理（TcpServer 的 map 持有 + 线程池任务可能暂持），
// 保证"连接关闭但还有任务要回包"时对象不会提前析构。
class TcpConnection : public Noncopyable,
                      public std::enable_shared_from_this<TcpConnection> {
 public:
  // 连接状态机：Connecting -> Connected -> (Disconnecting) -> Disconnected
  enum class State { kConnecting, kConnected, kDisconnecting, kDisconnected };

  using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
  using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
  using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;

  TcpConnection(EventLoop* loop, int fd, std::string name,
                const InetAddress& localAddr, const InetAddress& peerAddr);
  ~TcpConnection();

  EventLoop* loop() const { return loop_; }
  const std::string& name() const { return name_; }
  const InetAddress& localAddress() const { return localAddr_; }
  const InetAddress& peerAddress() const { return peerAddr_; }
  bool connected() const { return state_ == State::kConnected; }

  void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
  void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }
  void setWriteCompleteCallback(WriteCompleteCallback cb) {
    writeCompleteCallback_ = std::move(cb);
  }

  // 由 TcpServer 调用：连接建立完成（注册读事件）、从管理表移除时。
  void connectEstablished();
  void connectDestroyed();

  void send(const std::string& message);  // 线程安全：投递到 loop 线程真正发送
  void shutdown();                        // 优雅关闭：停止发送但可继续接收

 private:
  void handleRead();    // 读事件：readv 进 inputBuffer_ -> 消息回调
  void handleWrite();   // 写事件：尽量写完 outputBuffer_，写完关闭 EPOLLOUT
  void handleClose();   // 对端关闭/读 0：从 epoll 摘除并通知 TcpServer
  void handleError();   // 错误事件：打印 SO_ERROR
  void sendInLoop(const std::string& message);  // 真正写 socket（loop 线程内）
  void shutdownInLoop();                        // loop 线程内执行 shutdown
  void setState(State s) { state_ = s; }

  EventLoop* loop_;                  // 所属事件循环
  std::string name_;                 // 连接名（server-1 等，日志用）
  State state_;                      // 当前状态
  Socket socket_;                    // 连接 fd（RAII，析构时 close）
  std::unique_ptr<Channel> channel_; // 本连接的事件通道
  InetAddress localAddr_;            // 本地地址
  InetAddress peerAddr_;             // 对端地址
  Buffer inputBuffer_;               // 接收缓冲（粘包/半包攒数据）
  Buffer outputBuffer_;              // 发送缓冲（一次写不完时暂存）
  MessageCallback messageCallback_;      // 数据就绪回调（业务入口）
  CloseCallback closeCallback_;          // 关闭回调（TcpServer 用来移除连接）
  WriteCompleteCallback writeCompleteCallback_;  // 发送缓冲清空回调（可选）
};

}  // namespace minirpc
