#include "minirpc/net/TcpConnection.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

#include "minirpc/common/Logger.h"
#include "minirpc/net/Channel.h"
#include "minirpc/net/EventLoop.h"

namespace minirpc {

TcpConnection::TcpConnection(EventLoop* loop, int fd, std::string name,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
    : loop_(loop),
      name_(std::move(name)),
      state_(State::kConnecting),
      socket_(fd),
      channel_(std::make_unique<Channel>(loop, fd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr) {
  channel_->setReadCallback([this] { handleRead(); });
  channel_->setWriteCallback([this] { handleWrite(); });
  channel_->setErrorCallback([this] { handleError(); });
  channel_->setCloseCallback([this] { handleClose(); });
}

TcpConnection::~TcpConnection() {
  channel_->disableAll();
  LOG_TRACE << "TcpConnection destroyed: " << name_;
}

void TcpConnection::connectEstablished() {
  loop_->assertInLoopThread();
  setState(State::kConnected);
  channel_->enableReading();
}

void TcpConnection::connectDestroyed() {
  loop_->assertInLoopThread();
  if (state_ == State::kConnected) {
    setState(State::kDisconnected);
    channel_->disableAll();
  }
}

void TcpConnection::send(const std::string& message) {
  if (state_ != State::kConnected) {
    return;
  }
  std::weak_ptr<TcpConnection> weak = shared_from_this();
  loop_->runInLoop([weak, msg = message] {
    if (auto conn = weak.lock()) {
      conn->sendInLoop(msg);
    }
  });
}

void TcpConnection::sendInLoop(const std::string& message) {
  loop_->assertInLoopThread();
  if (state_ == State::kDisconnected) {
    return;
  }
  ssize_t nwritten = 0;
  size_t remaining = message.size();
  // 快速路径：输出缓冲为空且没在等写事件，直接 write 一次。
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    nwritten = ::write(socket_.fd(), message.data(), message.size());
    if (nwritten >= 0) {
      remaining = message.size() - static_cast<size_t>(nwritten);
      if (remaining == 0) {
        // 一次写完：通知写完成回调（可选）。
        if (writeCompleteCallback_) {
          writeCompleteCallback_(shared_from_this());
        }
        return;
      }
    } else {
      // EAGAIN 说明内核发送缓冲区满了，剩余数据进输出缓冲等 EPOLLOUT。
      nwritten = 0;
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR << "TcpConnection::sendInLoop write failed: "
                  << std::strerror(errno);
      }
    }
  }
  if (remaining > 0) {
    // 剩余部分进输出缓冲，并注册写事件（handleWrite 会继续发）。
    const char* start = message.data() + (message.size() - remaining);
    outputBuffer_.append(start, remaining);
    if (!channel_->isWriting()) {
      channel_->enableWriting();
    }
  }
}

void TcpConnection::shutdown() {
  if (state_ == State::kConnected) {
    setState(State::kDisconnecting);
    std::weak_ptr<TcpConnection> weak = shared_from_this();
    loop_->runInLoop([weak] {
      if (auto conn = weak.lock()) {
        conn->shutdownInLoop();
      }
    });
  }
}

void TcpConnection::shutdownInLoop() {
  loop_->assertInLoopThread();
  if (!channel_->isWriting()) {
    socket_.shutdownWrite();
  }
}

void TcpConnection::handleRead() {
  loop_->assertInLoopThread();
  int savedErrno = 0;
  const ssize_t n = inputBuffer_.readFd(socket_.fd(), &savedErrno);
  if (n > 0) {
    // 有数据：交给用户回调（echo/RPC 业务从这里进入）。
    if (messageCallback_) {
      messageCallback_(shared_from_this(), &inputBuffer_);
    }
  } else if (n == 0) {
    // 读到 0 = 对端关闭，触发关闭流程。
    handleClose();
  } else {
    // EAGAIN 是正常（非阻塞下读空了），其余才是真错误。
    if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) {
      return;
    }
    LOG_ERROR << "TcpConnection::handleRead error: "
              << std::strerror(savedErrno);
    handleClose();
  }
}

void TcpConnection::handleWrite() {
  loop_->assertInLoopThread();
  if (!channel_->isWriting()) {
    return;
  }
  // 尽量把输出缓冲写空；写空后关闭 EPOLLOUT，避免 busy loop。
  const ssize_t n =
      ::write(socket_.fd(), outputBuffer_.peek(), outputBuffer_.readableBytes());
  if (n > 0) {
    outputBuffer_.retrieve(static_cast<size_t>(n));
    if (outputBuffer_.readableBytes() == 0) {
      channel_->disableWriting();
      if (writeCompleteCallback_) {
        writeCompleteCallback_(shared_from_this());
      }
    }
  } else {
    LOG_ERROR << "TcpConnection::handleWrite failed: " << std::strerror(errno);
  }
}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  channel_->disableAll();  // 从 epoll 摘除，不再关心该 fd 的任何事件
  setState(State::kDisconnected);
  if (closeCallback_) {
    // 通知 TcpServer 从连接表移除；shared_from_this 保证回调期间对象存活。
    closeCallback_(shared_from_this());
  }
}

void TcpConnection::handleError() {
  int err = 0;
  socklen_t len = sizeof(err);
  if (::getsockopt(socket_.fd(), SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
    err = errno;
  }
  LOG_ERROR << "TcpConnection " << name_ << " error: " << std::strerror(err);
}

}  // namespace minirpc
