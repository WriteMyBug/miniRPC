#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class EventLoop;

// 多 Reactor 线程池：持有 N 个 EventLoop，每个在自己的线程中运行 loop()。
// 用途：把连接 IO 分发到多个 CPU 核，突破单 Reactor 的吞吐上限。
// 注意：EventLoop 必须在创建它的线程中运行，因此每个 loop 在各自线程内构造。
class EventLoopPool : public Noncopyable {
 public:
  explicit EventLoopPool(size_t threadCount);
  ~EventLoopPool();

  void start();               // 创建线程并启动各 EventLoop（幂等）
  EventLoop* getNext();       // 轮询返回下一个 loop（连接分发用）
  size_t size() const { return loops_.size(); }
  std::vector<EventLoop*> loops() const;

 private:
  size_t threadCount_;
  std::vector<std::shared_ptr<EventLoop>> loops_;
  std::vector<std::thread> threads_;
  std::atomic<size_t> next_{0};
  bool started_ = false;
};

}  // namespace minirpc

