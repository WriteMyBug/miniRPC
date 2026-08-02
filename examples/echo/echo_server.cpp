#include <csignal>
#include <cstdlib>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/net/TcpConnection.h"
#include "minirpc/net/TcpServer.h"

using namespace minirpc;

int main(int argc, char* argv[]) {
  uint16_t port = 8888;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  Logger::instance().setLogLevel(LogLevel::kInfo);

  EventLoop loop;
  TcpServer server(&loop, InetAddress(port), "echo");
  server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buffer) {
    const std::string msg = buffer->retrieveAllAsString();
    LOG_INFO << "echo " << msg.size() << " bytes from "
             << conn->peerAddress().toIpPort();
    conn->send(msg);
  });
  server.start();
  loop.loop();
  return 0;
}

