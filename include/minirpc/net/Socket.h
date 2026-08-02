#pragma once

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class InetAddress;

// 非阻塞、close-on-exec socket 的 RAII 封装。
class Socket : public Noncopyable {
 public:
  explicit Socket(int fd) : fd_(fd) {}
  ~Socket();

  int fd() const { return fd_; }

  void bindAddress(const InetAddress& addr);
  void listen();
  int accept(InetAddress* peerAddr);
  void shutdownWrite();

  void setReuseAddr(bool on);
  void setReusePort(bool on);
  void setKeepAlive(bool on);
  void setTcpNoDelay(bool on);

 private:
  int fd_;
};

}  // namespace minirpc

