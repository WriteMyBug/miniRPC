#include "minirpc/thread/ThreadPool.h"

#include <sstream>

namespace minirpc {

const char* ThreadPool::LogLevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo:  return "INFO";
    case LogLevel::kWarn:  return "WARN";
    case LogLevel::kError: return "ERROR";
    case LogLevel::kFatal: return "FATAL";
  }
  return "UNKNOWN";
}

void ThreadPool::Log(LogLevel level, const std::string& msg) {
  if (!logEnabled_) {
    return;
  }
  switch (level) {
    case LogLevel::kDebug: LOG_DEBUG << msg; break;
    case LogLevel::kInfo:  LOG_INFO << msg; break;
    case LogLevel::kWarn:  LOG_WARN << msg; break;
    case LogLevel::kError: LOG_ERROR << msg; break;
    default: break;
  }
}

ThreadPool::ThreadPool() = default;

ThreadPool::~ThreadPool() {
  // 关闭步骤：置停止标志 -> 唤醒所有等待线程 -> 等线程全部退出。
  // 等待谓词是"线程集合为空"，防止析构后工作线程还在访问成员。
  isRunning_ = false;
  Log(LogLevel::kInfo, "===== ThreadPool Stopped =====");
  notEmpty_.notify_all();
  notFull_.notify_all();
  std::unique_lock<std::mutex> lock(threadSetMutex_);
  exitCond_.wait(lock, [this]() { return threads_.empty(); });
  lock.unlock();
  // RAII 兜底：工作线程已全部退出，在析构线程执行剩余回调
  runPendingCallbacks();
}

void ThreadPool::SetMode(PoolMode mode) {
  if (isRunning_) return;
  mode_ = mode;
}

void ThreadPool::SetTaskQueueMaxSize(size_t maxSize) {
  if (isRunning_) return;
  maxTaskQueueSize_ = maxSize;
}

void ThreadPool::SetMaxThreadCount(size_t maxThreadCount) {
  if (isRunning_ || mode_ != PoolMode::kCached) return;
  maxThreadCount_ = maxThreadCount;
}

void ThreadPool::SetLogEnabled(bool enabled) {
  logEnabled_ = enabled;
}

// ── 统一入队（返回 false 表示超时）─────────────────────────────
bool ThreadPool::enqueueTask(int priority, std::function<void()> fn,
                             std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(taskQueueMutex_);
  // 队列满时最多等 timeout；等不到就返回 false（submit 会抛异常/trySubmitFor 返回空）。
  if (!notFull_.wait_for(lock, timeout,
                         [this] {
                           return taskQueueSize_ < maxTaskQueueSize_ ||
                                  !isRunning_;
                         })) {
    return false;
  }
  if (!isRunning_) {
    return false;
  }
  taskQueue_.push({priority, std::move(fn)});
  taskQueueSize_++;
  notEmpty_.notify_all();  // 唤醒一个/多个工作线程去取任务

  // cached 模式：任务堆积且有空闲线程配额时动态扩线程。
  if (mode_ == PoolMode::kCached && taskQueueSize_ > idleThreadCount_ &&
      currentThreadCount_ < maxThreadCount_) {
    std::ostringstream oss;
    oss << "creating new thread (queue=" << taskQueueSize_
        << ", idle=" << idleThreadCount_ << ")";
    Log(LogLevel::kInfo, oss.str());
    CreateThread();
  }
  return true;
}

// ── 等待工作（FIXED 阻塞 / CACHED 超时）────────────────────────
// 在 taskQueueMutex_ 锁内调用；返回 false 表示调用线程应退出
bool ThreadPool::waitForWork(std::unique_lock<std::mutex>& lock,
                             const std::function<bool()>& hasWork) {
  if (mode_ == PoolMode::kCached && currentThreadCount_ > initThreadSize_) {
    // 额外线程：60s 无任务则退出
    if (!notEmpty_.wait_for(lock, std::chrono::seconds(60), hasWork)) {
      return false;
    }
  } else {
    notEmpty_.wait(lock, hasWork);
  }
  return true;
}

// ── Worker ──────────────────────────────────────────────────────
void ThreadPool::WorkerThread() {
  const std::thread::id threadId = std::this_thread::get_id();
  for (;;) {
    if (!isRunning_) break;
    std::function<void()> taskFn;
    {
      std::unique_lock<std::mutex> lock(taskQueueMutex_);
      // 队列空则等待；谓词包含 !isRunning_ 保证关闭时能退出等待。
      while (taskQueueSize_ == 0) {
        if (!waitForWork(lock, [this]() {
              return taskQueueSize_ > 0 || !isRunning_;
            })) {
          threadExit(threadId);
          exitCond_.notify_all();
          return;
        }
        if (!isRunning_) break;
      }
      if (!isRunning_) break;

      idleThreadCount_--;
      // 取优先级最高的任务（priority_queue 堆顶）。
      taskFn = std::move(const_cast<PrioritizedTask&>(taskQueue_.top()).func);
      taskQueue_.pop();
      taskQueueSize_--;
    }
    notFull_.notify_all();

    if (taskFn) {
      try {
        taskFn();  // 执行用户任务
      } catch (const std::exception& e) {
        // 任务异常不能让工作线程死掉：记录日志，异常通过 future 传给调用方。
        Log(LogLevel::kError,
            std::string("Unhandled exception in task: ") + e.what());
      } catch (...) {
        Log(LogLevel::kError, "Unhandled unknown exception in task");
      }
    }
    idleThreadCount_++;
  }
  threadExit(threadId);
  exitCond_.notify_all();
}

// ── 启动线程池 ───────────────────────────────────────────────────
void ThreadPool::Start(const ThreadPoolConfig& config) {
  isRunning_ = true;
  mode_ = config.mode;
  initThreadSize_ = config.initThreadCount;
  maxThreadCount_ = config.maxThreadCount;
  maxTaskQueueSize_ = config.maxQueueSize;

  Log(LogLevel::kInfo, "===== ThreadPool Started =====");
  {
    std::ostringstream oss;
    oss << "Mode: " << (mode_ == PoolMode::kFixed ? "FIXED" : "CACHED");
    Log(LogLevel::kInfo, oss.str());
  }
  {
    std::ostringstream oss;
    oss << "Initial thread count: " << initThreadSize_;
    Log(LogLevel::kInfo, oss.str());
  }
  {
    std::ostringstream oss;
    oss << "Max thread count: " << maxThreadCount_;
    Log(LogLevel::kInfo, oss.str());
  }
  {
    std::ostringstream oss;
    oss << "Max task queue size: " << maxTaskQueueSize_;
    Log(LogLevel::kInfo, oss.str());
  }
  Log(LogLevel::kInfo, "=============================");

  for (size_t i = 0; i < initThreadSize_; ++i) {
    CreateThread();
  }
}

void ThreadPool::Start(size_t initThreadSize) {
  ThreadPoolConfig config;
  config.mode = mode_;
  config.initThreadCount = initThreadSize;
  config.maxThreadCount = maxThreadCount_;
  config.maxQueueSize = maxTaskQueueSize_;
  Start(config);
}

// ── 创建线程（先注册 id 再 detach，避免析构时死等）──────────────
void ThreadPool::CreateThread() {
  {
    std::unique_lock<std::mutex> lock(threadSetMutex_);
    if (currentThreadCount_ >= maxThreadCount_) return;
    currentThreadCount_++;
    idleThreadCount_++;
  }
  std::thread t([this] { WorkerThread(); });
  const std::thread::id threadId = t.get_id();
  // 先注册 id 再 detach（修复原实现竞态：先 detach 可能让析构死等，见 decisions D1）。
  {
    std::unique_lock<std::mutex> lock(threadSetMutex_);
    threads_.insert(threadId);
  }
  t.detach();
  std::ostringstream oss;
  oss << "Thread " << threadId << " started.";
  Log(LogLevel::kInfo, oss.str());
}

void ThreadPool::threadExit(std::thread::id threadId) {
  std::unique_lock<std::mutex> lock(threadSetMutex_);
  auto it = threads_.find(threadId);
  if (it == threads_.end()) return;
  std::ostringstream oss;
  oss << "Thread " << threadId << " exit.";
  Log(LogLevel::kInfo, oss.str());
  threads_.erase(it);
  currentThreadCount_--;
  idleThreadCount_--;
}

// ── 回调队列处理 ─────────────────────────────────────────────────
void ThreadPool::runPendingCallbacks() {
  std::queue<std::function<void()>> pending;
  {
    // 整队 swap 出来再执行：回调里可能又投递新回调，避免死锁。
    std::lock_guard<std::mutex> lk(callbackQueueMutex_);
    pending.swap(callbackQueue_);
  }
  while (!pending.empty()) {
    pending.front()();
    pending.pop();
  }
}

}  // namespace minirpc
