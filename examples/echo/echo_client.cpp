#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr int kRecvTimeoutSec = 5;

}  // namespace

// 用法：echo_client [ip] [port]
// 从 stdin 逐行读取并发送，收到服务端回显后打印。
int main(int argc, char* argv[]) {
  const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
  const uint16_t port =
      (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 8888;

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "socket failed" << std::endl;
    return 1;
  }
  timeval timeout{kRecvTimeoutSec, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    std::cerr << "invalid ip: " << ip << std::endl;
    ::close(fd);
    return 1;
  }
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) <
      0) {
    std::cerr << "connect failed" << std::endl;
    ::close(fd);
    return 1;
  }

  std::string line;
  std::string response;
  char buf[4096];
  while (std::getline(std::cin, line)) {
    line += '\n';
    if (::send(fd, line.data(), line.size(), 0) < 0) {
      std::cerr << "send failed" << std::endl;
      break;
    }
    // 读到完整一行回显为止。
    response.clear();
    bool echoed = false;
    while (response.find('\n') == std::string::npos) {
      const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) {
        if (n < 0) {
          std::cerr << "recv failed" << std::endl;
        }
        break;
      }
      response.append(buf, static_cast<size_t>(n));
    }
    if (!response.empty()) {
      std::cout << response;
      echoed = true;
    }
    if (!echoed) {
      break;
    }
  }
  ::close(fd);
  return 0;
}

