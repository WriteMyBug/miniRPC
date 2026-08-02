#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <netinet/in.h>
#include <string>

namespace minirpc {

// IPv4 地址封装。
class InetAddress {
 public:
  InetAddress() = default;
  explicit InetAddress(uint16_t port, bool loopbackOnly = false);
  InetAddress(const std::string& ip, uint16_t port);
  explicit InetAddress(const sockaddr_in& addr) : addr_(addr) {}

  std::string toIp() const;
  std::string toIpPort() const;
  uint16_t port() const { return ntohs(addr_.sin_port); }

  const sockaddr_in& sockAddr() const { return addr_; }
  sockaddr_in* sockAddr() { return &addr_; }

 private:
  sockaddr_in addr_{};
};

}  // namespace minirpc

