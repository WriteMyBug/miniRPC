#include "minirpc/net/TcpServer.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include "minirpc/common/Logger.h"
#include "minirpc/net/Channel.h"
#include "minirpc/net/EventLoop.h"

namespace minirpc {

namespace {

int createNonblockingSocket() {
  const int fd =
      ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    LOG_FATAL << "socket() failed: " << std::strerror(errno);
    ::abort();
  }
  return fd;
}

}  // namespace

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr,
                     std::string name)
    : loop_(loop),
      name_(std::move(name)),
      listenAddr_(listenAddr),
      listenSocket_(createNonblockingSocket()),
      acceptChannel_(std::make_unique<Channel>(loop_, listenSocket_.fd())) {
  listenSocket_.setReuseAddr(true);
  listenSocket_.setReusePort(true);
  acceptChannel_->setReadCallback([this] { handleNewConnection(); });
}

TcpServer::~TcpServer() = default;

void TcpServer::start() {
  if (started_) {
    return;
  }
  started_ = true;
  // 忽略 SIGPIPE：向已关闭的连接写数据默认会杀进程，改为让 write 返回 EPIPE。
  std::signal(SIGPIPE, SIG_IGN);
  listenSocket_.bindAddress(listenAddr_);
  listenSocket_.listen();
  // 监听 fd 注册读事件：有新连接时 handleNewConnection 被回调。
  acceptChannel_->enableReading();
  LOG_INFO << "TcpServer[" << name_ << "] listening on "
           << listenAddr_.toIpPort();
}

void TcpServer::handleNewConnection() {
  loop_->assertInLoopThread();
  // accept 循环：非阻塞下尽量把积压的连接都收完，EAGAIN 再停。
  while (true) {
    InetAddress peerAddr;
    const int connfd = listenSocket_.accept(&peerAddr);
    if (connfd < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR << "accept failed: " << std::strerror(errno);
      }
      break;
    }
    const std::string connName =
        name_ + "-" + std::to_string(nextConnId_++);
    auto conn =
        std::make_shared<TcpConnection>(loop_, connfd, connName,
                                        listenAddr_, peerAddr);
    conn->setMessageCallback(messageCallback_);
    // 连接关闭时自动从连接表移除（跨线程安全投递）。
    conn->setCloseCallback(
        [this](const TcpConnectionPtr& c) { removeConnection(c); });
    connections_[connName] = conn;
    // 状态置 Connected 并注册读事件，之后才开始收数据。
    conn->connectEstablished();
    if (connectionCallback_) {
      connectionCallback_(conn);
    }
    LOG_INFO << "TcpServer: new connection " << connName << " from "
             << peerAddr.toIpPort();
  }
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
  loop_->runInLoop([this, conn] { removeConnectionInLoop(conn); });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer: connection " << conn->name() << " closed";
  // erase 后 map 里的引用消失；若线程池任务还持有 conn，对象会等到任务结束才析构。
  connections_.erase(conn->name());
  conn->connectDestroyed();
}

}  // namespace minirpc
