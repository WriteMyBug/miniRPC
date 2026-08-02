#include <csignal>
#include <cstdlib>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/registry/RegistryServiceImpl.h"
#include "minirpc/rpc/RpcServer.h"

using namespace minirpc;

// 用法：registry_server [port] [heartbeat_timeout_ms]
int main(int argc, char* argv[]) {
  uint16_t port = 18800;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  std::chrono::milliseconds heartbeatTimeout(6000);
  if (argc > 2) {
    heartbeatTimeout = std::chrono::milliseconds(std::atoi(argv[2]));
  }
  Logger::instance().setLogLevel(LogLevel::kInfo);
  std::signal(SIGPIPE, SIG_IGN);

  EventLoop loop;
  RpcServer server(&loop, InetAddress(port), "registry");
  RegistryServiceImpl registry(heartbeatTimeout);
  server.registerService(&registry);
  server.start();
  LOG_INFO << "Registry listening on port " << port
           << ", heartbeat timeout " << heartbeatTimeout.count() << "ms";
  loop.loop();
  return 0;
}

