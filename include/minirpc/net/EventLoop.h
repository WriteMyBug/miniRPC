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

// 事件循环：一个线程一个 EventLoop，是整个网络层的"心脏"。
// 职责：epoll_wait 等待事件 -> 分发给 Channel 回调 -> 执行其他线程投递的
// pending 任务。网络层除任务队列外几乎无锁，就是因为所有 IO 都串在这个线程里。
class EventLoop : public Noncopyable {
 public:
  using Functor = std::function<void()>;

  EventLoop();
  ~EventLoop();

  void loop();  // 阻塞运行事件循环（创建该对象的线程调用）
  void quit();  // 线程安全：置退出标志，必要时唤醒

  // 在 loop 线程内执行；跨线程调用会入队并通过 eventfd 唤醒。
  void runInLoop(Functor cb);
  void queueInLoop(Functor cb);

  // 由 Channel 调用：把 fd 的感兴趣事件同步到 epoll。
  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);

  // 调试/防御：确保只能在 loop 线程访问网络层对象。
  void assertInLoopThread();
  bool isInLoopThread() const;

 private:
  void wakeup();        // 向 eventfd 写 1 字节，打断阻塞中的 epoll_wait
  void handleWakeup();  // 读空 eventfd，消费唤醒事件
  void doPendingFunctors();  // 取出并执行 pending 任务（加锁 swap，避免长时间持锁）

  std::atomic<bool> looping_{false};       // 是否正在跑 loop
  std::atomic<bool> quit_{false};          // 退出标志（跨线程读写）
  std::thread::id threadId_;               // 创建该 loop 的线程 id
  std::unique_ptr<EpollPoller> poller_;    // epoll 封装
  int wakeupFd_;                           // eventfd：跨线程唤醒用
  std::unique_ptr<Channel> wakeupChannel_; // wakeupFd 对应的 Channel
  std::vector<Functor> pendingFunctors_;   // 待执行任务
  std::mutex mutex_;                       // 保护 pendingFunctors_
};

}  // namespace minirpc
