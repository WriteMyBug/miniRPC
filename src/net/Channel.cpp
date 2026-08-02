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
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    if (closeCallback_) {
      closeCallback_();
    }
    return;
  }
  if (revents_ & EPOLLERR) {
    if (errorCallback_) {
      errorCallback_();
    }
  }
  if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
    if (readCallback_) {
      readCallback_();
    }
  }
  if (revents_ & EPOLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    }
  }
}

}  // namespace minirpc

