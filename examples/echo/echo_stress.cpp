#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kRecvTimeoutSec = 10;

}  // namespace

// 并发压力验证：用法 echo_stress [ip] [port] [clients] [messages]
// 每个客户端线程建立连接，发送 messages 条消息并校验回显一致。
int main(int argc, char* argv[]) {
  const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
  const uint16_t port =
      (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 8888;
  const int clients = (argc > 3) ? std::atoi(argv[3]) : 8;
  const int messages = (argc > 4) ? std::atoi(argv[4]) : 20;

  std::atomic<int> failed{0};
  auto worker = [&](int id) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      failed.fetch_add(1);
      return;
    }
    timeval timeout{kRecvTimeoutSec, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) <
            0) {
      failed.fetch_add(1);
      ::close(fd);
      return;
    }

    for (int i = 0; i < messages; ++i) {
      const std::string msg =
          "client-" + std::to_string(id) + "-msg-" + std::to_string(i) + "\n";
      if (::send(fd, msg.data(), msg.size(), 0) < 0) {
        failed.fetch_add(1);
        break;
      }
      std::string echoed;
      char buf[1024];
      while (echoed.find('\n') == std::string::npos) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
          break;
        }
        echoed.append(buf, static_cast<size_t>(n));
      }
      if (echoed != msg) {
        failed.fetch_add(1);
        break;
      }
    }
    ::close(fd);
  };

  std::vector<std::thread> threads;
  threads.reserve(clients);
  for (int i = 0; i < clients; ++i) {
    threads.emplace_back(worker, i);
  }
  for (auto& t : threads) {
    t.join();
  }

  const long total = static_cast<long>(clients) * messages;
  std::cout << "echo_stress: " << clients << " clients x " << messages
            << " messages = " << total << " requests, failed="
            << failed.load() << std::endl;
  return failed.load() == 0 ? 0 : 1;
}

