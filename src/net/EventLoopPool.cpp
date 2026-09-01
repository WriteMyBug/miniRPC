#include "minirpc/net/EventLoopPool.h"

#include <future>
#include <utility>

#include "minirpc/net/EventLoop.h"

namespace minirpc {

EventLoopPool::EventLoopPool(size_t threadCount)
    : threadCount_(threadCount == 0 ? 1 : threadCount) {
  loops_.reserve(threadCount_);
  threads_.reserve(threadCount_);
}

EventLoopPool::~EventLoopPool() {
  // 先置退出标志（EventLoop::quit 线程安全，会唤醒 epoll_wait），再回收线程。
  for (const auto& loop : loops_) {
    loop->quit();
  }
  for (auto& t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
}

void EventLoopPool::start() {
  if (started_) {
    return;
  }
  started_ = true;
  for (size_t i = 0; i < threadCount_; ++i) {
    std::promise<std::shared_ptr<EventLoop>> ready;
    auto fut = ready.get_future();
    // EventLoop 在线程内构造，保证 threadId_ 与运行线程一致（assertInLoopThread 要求）。
    threads_.emplace_back([this, p = std::move(ready)]() mutable {
      auto loop = std::make_shared<EventLoop>();
      p.set_value(loop);
      loop->loop();
    });
    loops_.push_back(fut.get());
  }
}

EventLoop* EventLoopPool::getNext() {
  if (loops_.empty()) {
    return nullptr;
  }
  return loops_[next_.fetch_add(1) % loops_.size()].get();
}

std::vector<EventLoop*> EventLoopPool::loops() const {
  std::vector<EventLoop*> result;
  result.reserve(loops_.size());
  for (const auto& loop : loops_) {
    result.push_back(loop.get());
  }
  return result;
}

}  // namespace minirpc

