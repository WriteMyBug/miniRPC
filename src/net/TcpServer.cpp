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
  std::signal(SIGPIPE, SIG_IGN);
  listenSocket_.bindAddress(listenAddr_);
  listenSocket_.listen();
  acceptChannel_->enableReading();
  LOG_INFO << "TcpServer[" << name_ << "] listening on "
           << listenAddr_.toIpPort();
}

void TcpServer::handleNewConnection() {
  loop_->assertInLoopThread();
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
    conn->setCloseCallback(
        [this](const TcpConnectionPtr& c) { removeConnection(c); });
    connections_[connName] = conn;
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
  connections_.erase(conn->name());
  conn->connectDestroyed();
}

}  // namespace minirpc

