#include "minirpc/net/EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "minirpc/common/Logger.h"
#include "minirpc/net/Channel.h"
#include "minirpc/net/EpollPoller.h"

namespace minirpc {

EventLoop::EventLoop()
    : threadId_(std::this_thread::get_id()),
      poller_(std::make_unique<EpollPoller>()),
      wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_)) {
  if (wakeupFd_ < 0) {
    LOG_FATAL << "eventfd failed: " << std::strerror(errno);
    ::abort();
  }
  wakeupChannel_->setReadCallback([this] { handleWakeup(); });
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
}

void EventLoop::loop() {
  assertInLoopThread();
  looping_ = true;
  quit_ = false;
  std::vector<Channel*> activeChannels;
  while (!quit_) {
    activeChannels.clear();
    poller_->poll(10000, &activeChannels);
    for (Channel* channel : activeChannels) {
      channel->handleEvent();
    }
    doPendingFunctors();
  }
  looping_ = false;
}

void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup();
  }
}

void EventLoop::runInLoop(Functor cb) {
  if (isInLoopThread()) {
    cb();
  } else {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(std::move(cb));
  }
  if (!isInLoopThread() || !looping_) {
    wakeup();
  }
}

void EventLoop::updateChannel(Channel* channel) {
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
  poller_->removeChannel(channel);
}

void EventLoop::assertInLoopThread() {
  if (!isInLoopThread()) {
    LOG_FATAL << "EventLoop accessed from wrong thread";
    ::abort();
  }
}

bool EventLoop::isInLoopThread() const {
  return threadId_ == std::this_thread::get_id();
}

void EventLoop::wakeup() {
  const uint64_t one = 1;
  if (::write(wakeupFd_, &one, sizeof(one)) < 0 && errno != EAGAIN) {
    LOG_ERROR << "EventLoop::wakeup failed: " << std::strerror(errno);
  }
}

void EventLoop::handleWakeup() {
  uint64_t one = 0;
  while (::read(wakeupFd_, &one, sizeof(one)) > 0) {
  }
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }
  for (Functor& f : functors) {
    f();
  }
}

}  // namespace minirpc

