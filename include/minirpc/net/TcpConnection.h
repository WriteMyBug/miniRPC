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

// 一条已建立的 TCP 连接：负责读写、缓冲与生命周期回调。
class TcpConnection : public Noncopyable,
                      public std::enable_shared_from_this<TcpConnection> {
 public:
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

  // 由 TcpServer 调用：连接建立完成、从管理表移除时。
  void connectEstablished();
  void connectDestroyed();

  void send(const std::string& message);
  void shutdown();

 private:
  void handleRead();
  void handleWrite();
  void handleClose();
  void handleError();
  void sendInLoop(const std::string& message);
  void shutdownInLoop();
  void setState(State s) { state_ = s; }

  EventLoop* loop_;
  std::string name_;
  State state_;
  Socket socket_;
  std::unique_ptr<Channel> channel_;
  InetAddress localAddr_;
  InetAddress peerAddr_;
  Buffer inputBuffer_;
  Buffer outputBuffer_;
  MessageCallback messageCallback_;
  CloseCallback closeCallback_;
  WriteCompleteCallback writeCompleteCallback_;
};

}  // namespace minirpc

