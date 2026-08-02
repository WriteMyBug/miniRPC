#include "minirpc/net/InetAddress.h"

#include <cstring>

namespace minirpc {

InetAddress::InetAddress(uint16_t port, bool loopbackOnly) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);
  addr_.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
}

InetAddress::InetAddress(const std::string& ip, uint16_t port) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr) != 1) {
    addr_.sin_addr.s_addr = htonl(INADDR_ANY);
  }
}

std::string InetAddress::toIp() const {
  char buf[INET_ADDRSTRLEN];
  ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
  return buf;
}

std::string InetAddress::toIpPort() const {
  return toIp() + ":" + std::to_string(port());
}

}  // namespace minirpc

