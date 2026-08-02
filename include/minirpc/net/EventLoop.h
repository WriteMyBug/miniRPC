#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class Channel;
class EpollPoller;

// 事件循环：一个线程一个 EventLoop，负责 poll + 分发事件 + 执行 pending 任务。
class EventLoop : public Noncopyable {
 public:
  using Functor = std::function<void()>;

  EventLoop();
  ~EventLoop();

  void loop();
  void quit();

  // 在 loop 线程内执行；跨线程调用会入队并通过 eventfd 唤醒。
  void runInLoop(Functor cb);
  void queueInLoop(Functor cb);

  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);

  void assertInLoopThread();
  bool isInLoopThread() const;

 private:
  void wakeup();
  void handleWakeup();
  void doPendingFunctors();

  std::atomic<bool> looping_{false};
  std::atomic<bool> quit_{false};
  std::thread::id threadId_;
  std::unique_ptr<EpollPoller> poller_;
  int wakeupFd_;
  std::unique_ptr<Channel> wakeupChannel_;
  std::vector<Functor> pendingFunctors_;
  std::mutex mutex_;
};

}  // namespace minirpc

