#include "minirpc/net/Channel.h"

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"

namespace minirpc {

Channel::Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

void Channel::update() {
  loop_->updateChannel(this);
}

void Channel::remove() {
  loop_->removeChannel(this);
}

void Channel::handleEvent() {
  // 对端关闭（HUP）且没有可读数据：优先走关闭回调，避免误触发读回调。
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    if (closeCallback_) {
      closeCallback_();
    }
    return;
  }
  // 出错事件（如对端 RST）：通知错误回调（连接层打印 SO_ERROR）。
  if (revents_ & EPOLLERR) {
    if (errorCallback_) {
      errorCallback_();
    }
  }
  // 可读（含对端关闭读方向的 RDHUP）：走读回调（TcpConnection::handleRead）。
  if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
    if (readCallback_) {
      readCallback_();
    }
  }
  // 可写：走写回调（TcpConnection::handleWrite 清空发送缓冲）。
  if (revents_ & EPOLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    }
  }
}

}  // namespace minirpc
