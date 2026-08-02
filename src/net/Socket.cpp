#include "minirpc/net/Socket.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "minirpc/common/Logger.h"
#include "minirpc/net/InetAddress.h"

namespace minirpc {

Socket::~Socket() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

void Socket::bindAddress(const InetAddress& addr) {
  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr.sockAddr()),
             sizeof(sockaddr_in)) < 0) {
    LOG_FATAL << "Socket::bind(" << addr.toIpPort()
              << ") failed: " << std::strerror(errno);
    ::abort();
  }
}

void Socket::listen() {
  if (::listen(fd_, SOMAXCONN) < 0) {
    LOG_FATAL << "Socket::listen failed: " << std::strerror(errno);
    ::abort();
  }
}

int Socket::accept(InetAddress* peerAddr) {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  const int connfd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&addr), &len,
                               SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (connfd >= 0 && peerAddr != nullptr) {
    *peerAddr = InetAddress(addr);
  }
  return connfd;
}

void Socket::shutdownWrite() {
  if (::shutdown(fd_, SHUT_WR) < 0) {
    LOG_ERROR << "Socket::shutdownWrite failed: " << std::strerror(errno);
  }
}

void Socket::setReuseAddr(bool on) {
  const int opt = on ? 1 : 0;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

void Socket::setReusePort(bool on) {
  const int opt = on ? 1 : 0;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
}

void Socket::setKeepAlive(bool on) {
  const int opt = on ? 1 : 0;
  ::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
}

void Socket::setTcpNoDelay(bool on) {
  const int opt = on ? 1 : 0;
  ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

}  // namespace minirpc
