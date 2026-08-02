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
  // eventfd 创建失败是致命错误：没有它跨线程无法唤醒事件循环。
  if (wakeupFd_ < 0) {
    LOG_FATAL << "eventfd failed: " << std::strerror(errno);
    ::abort();
  }
  // wakeup 通道只关心可读：被唤醒后读空它，不触发任何业务回调。
  wakeupChannel_->setReadCallback([this] { handleWakeup(); });
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
}

void EventLoop::loop() {
  assertInLoopThread();  // 防止在别的线程里跑 loop（线程模型错误会立即暴露）
  looping_ = true;
  quit_ = false;
  std::vector<Channel*> activeChannels;
  while (!quit_) {
    activeChannels.clear();  // 复用 vector，减少分配
    // poll 超时 10s：即使一直没网络事件，也能周期性执行 pending 任务。
    poller_->poll(10000, &activeChannels);
    // 依次分发就绪事件；回调里可能修改连接表/Channel，但都是本线程操作。
    for (Channel* channel : activeChannels) {
      channel->handleEvent();
    }
    // 最后执行跨线程投递的任务（如线程池回包）。
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
    cb();  // 已经在 loop 线程：直接执行，避免一次无谓唤醒
  } else {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(std::move(cb));
  }
  // 跨线程（或 loop 还没开始跑）时唤醒，让 epoll_wait 立即返回。
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
  // eventfd 写 1 字节；缓冲区满（EAGAIN）说明已经有唤醒在途，忽略即可。
  if (::write(wakeupFd_, &one, sizeof(one)) < 0 && errno != EAGAIN) {
    LOG_ERROR << "EventLoop::wakeup failed: " << std::strerror(errno);
  }
}

void EventLoop::handleWakeup() {
  uint64_t one = 0;
  // 读空 eventfd，否则会一直触发 EPOLLIN。
  while (::read(wakeupFd_, &one, sizeof(one)) > 0) {
  }
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  {
    // swap 出队列再执行：避免执行回调期间一直持锁（回调可能又 queueInLoop）。
    std::lock_guard<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }
  for (Functor& f : functors) {
    f();
  }
}

}  // namespace minirpc
