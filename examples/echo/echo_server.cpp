#include <csignal>
#include <cstdlib>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/net/TcpConnection.h"
#include "minirpc/net/TcpServer.h"
#include "minirpc/thread/ThreadPool.h"

using namespace minirpc;

int main(int argc, char* argv[]) {
  uint16_t port = 8888;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  Logger::instance().setLogLevel(LogLevel::kInfo);

  // 第 2 周：消息处理下沉到线程池（主 reactor 只负责 IO 与分发）。
  ThreadPool pool;
  pool.SetLogEnabled(false);
  pool.Start(4);

  EventLoop loop;
  TcpServer server(&loop, InetAddress(port), "echo");
  server.setMessageCallback([&pool](const TcpConnectionPtr& conn,
                                    Buffer* buffer) {
    const std::string msg = buffer->retrieveAllAsString();
    pool.submit([conn, msg] {
      LOG_INFO << "echo " << msg.size() << " bytes (pool thread)";
      conn->send(msg);
    });
  });
  server.start();
  loop.loop();
  return 0;
}
