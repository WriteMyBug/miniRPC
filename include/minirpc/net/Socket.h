#pragma once

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class InetAddress;

// 非阻塞、close-on-exec socket 的 RAII 封装。
// 职责：bind/listen/accept/半关闭 + 常用 socket 选项；
// 析构时自动 close(fd)，防止 fd 泄漏。
class Socket : public Noncopyable {
 public:
  explicit Socket(int fd) : fd_(fd) {}
  ~Socket();

  int fd() const { return fd_; }

  void bindAddress(const InetAddress& addr);  // bind（失败会 FATAL，监听地址写错要暴露）
  void listen();                              // listen（SOMAXCONN 积压队列）
  int accept(InetAddress* peerAddr);          // accept4：新 fd 直接设非阻塞+CLOEXEC
  void shutdownWrite();                       // SHUT_WR：只关发送方向，还能收

  // 常用 TCP 选项，见函数名
  void setReuseAddr(bool on);
  void setReusePort(bool on);
  void setKeepAlive(bool on);
  void setTcpNoDelay(bool on);

 private:
  int fd_;
};

}  // namespace minirpc
