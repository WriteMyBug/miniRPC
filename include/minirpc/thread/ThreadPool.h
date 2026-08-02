#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "minirpc/common/Logger.h"

namespace minirpc {

// 固定数量线程池 / 动态大小线程池
enum class PoolMode {
  kFixed,
  kCached,
};

// 任务优先级（值越小优先级越高）
enum class TaskPriority {
  kHigh = 0,
  kNormal = 1,
  kLow = 2,
};

// 全局优先级队列任务包装
struct PrioritizedTask {
  int priority;
  mutable std::function<void()> func;

  bool operator<(const PrioritizedTask& other) const {
    return priority > other.priority;  // priority_queue 顶部为最高优先级
  }
};

// 线程池配置（一次性传入，避免 setter 在 Start 后被静默忽略）
struct ThreadPoolConfig {
  PoolMode mode = PoolMode::kFixed;
  size_t initThreadCount = std::thread::hardware_concurrency();
  size_t maxThreadCount = 8;
  size_t maxQueueSize = 1024;
};

// 复用自 /home/nina/cpp_learn/threadpool（用户已完成的项目），
// 移植到 minirpc 命名空间、接入统一日志，并修复 CreateThread 竞态。
class ThreadPool {
 public:
  ThreadPool();
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void SetMode(PoolMode mode);
  void SetTaskQueueMaxSize(size_t maxSize);
  void SetMaxThreadCount(size_t maxThreadCount);
  void SetLogEnabled(bool enabled);

  void Start(const ThreadPoolConfig& config);
  void Start(size_t initThreadSize);

  // 接受任意可调用对象，返回 std::future
  template <typename F, typename... Args>
  auto submit(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<std::decay_t<F>,
                                          std::decay_t<Args>...>> {
    return submitWithPriority(TaskPriority::kNormal, std::forward<F>(f),
                              std::forward<Args>(args)...);
  }

  template <typename F, typename... Args>
  auto submitWithPriority(TaskPriority priority, F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<std::decay_t<F>,
                                          std::decay_t<Args>...>> {
    using ReturnType =
        std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(
        [f = std::forward<F>(f),
         tup = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType {
          return std::apply(std::move(f), std::move(tup));
        });
    std::future<ReturnType> fut = taskPtr->get_future();
    if (!enqueueTask(static_cast<int>(priority), [taskPtr]() { (*taskPtr)(); })) {
      throw std::runtime_error("ThreadPool: queue full, timed out");
    }
    return fut;
  }

  // 带超时提交：队列满超时返回 nullopt（不抛异常）
  template <typename F, typename... Args>
  auto trySubmitFor(std::chrono::milliseconds timeout, F&& f, Args&&... args)
      -> std::optional<std::future<
          std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>> {
    using ReturnType =
        std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(
        [f = std::forward<F>(f),
         tup = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType {
          return std::apply(std::move(f), std::move(tup));
        });
    std::future<ReturnType> fut = taskPtr->get_future();
    if (!enqueueTask(static_cast<int>(TaskPriority::kNormal),
                     [taskPtr]() { (*taskPtr)(); }, timeout)) {
      return std::nullopt;
    }
    return std::optional<std::future<ReturnType>>(std::move(fut));
  }

  // ── 回调模式 ──
  // 任务在工作线程执行，结果 + 回调投递到独立队列；
  // 主线程调用 runPendingCallbacks() 在自己的上下文执行回调。
  template <typename F, typename C, typename... Args>
  void submitWithCallback(TaskPriority priority, F&& f, C&& callback,
                          Args&&... args) {
    using ReturnType =
        std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    auto taskFn = [f = std::forward<F>(f), cb = std::forward<C>(callback),
                   tup = std::make_tuple(std::forward<Args>(args)...),
                   this]() mutable {
      if constexpr (std::is_void_v<ReturnType>) {
        std::apply(std::move(f), std::move(tup));
        std::lock_guard<std::mutex> lk(callbackQueueMutex_);
        callbackQueue_.push(std::move(cb));
      } else {
        auto result = std::apply(std::move(f), std::move(tup));
        std::lock_guard<std::mutex> lk(callbackQueueMutex_);
        callbackQueue_.push([cb = std::move(cb), r = std::move(result)]() mutable {
          std::move(cb)(std::move(r));
        });
      }
    };
    if (!enqueueTask(static_cast<int>(priority), std::move(taskFn))) {
      throw std::runtime_error("ThreadPool: queue full, timed out");
    }
  }

  template <typename F, typename C, typename... Args>
  void submitWithCallback(F&& f, C&& callback, Args&&... args) {
    submitWithCallback(TaskPriority::kNormal, std::forward<F>(f),
                       std::forward<C>(callback), std::forward<Args>(args)...);
  }

  // 主线程在其事件循环中调用，批量执行待处理回调
  void runPendingCallbacks();
  void Log(LogLevel level, const std::string& msg);

 private:
  static const char* LogLevelToString(LogLevel level);
  void WorkerThread();
  void CreateThread();
  void threadExit(std::thread::id);
  bool enqueueTask(int priority, std::function<void()> fn,
                   std::chrono::milliseconds timeout = std::chrono::seconds(60));
  bool waitForWork(std::unique_lock<std::mutex>& lock,
                   const std::function<bool()>& hasWork);

  size_t initThreadSize_ = 0;
  std::unordered_set<std::thread::id> threads_;
  PoolMode mode_ = PoolMode::kFixed;
  std::atomic<bool> isRunning_{false};
  bool logEnabled_ = true;
  size_t maxThreadCount_ = 8;
  std::atomic<size_t> currentThreadCount_{0};
  std::atomic<size_t> idleThreadCount_{0};

  // ── 全局优先级队列 ──
  std::atomic<size_t> taskQueueSize_{0};
  std::priority_queue<PrioritizedTask> taskQueue_;
  size_t maxTaskQueueSize_ = 1024;

  std::mutex taskQueueMutex_;
  std::mutex threadSetMutex_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
  std::condition_variable exitCond_;

  // 回调队列（独立锁，与任务队列锁隔离）
  std::queue<std::function<void()>> callbackQueue_;
  std::mutex callbackQueueMutex_;
};

}  // namespace minirpc

