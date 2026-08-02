#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "minirpc/thread/ThreadPool.h"

using namespace minirpc;

namespace {

int failures = 0;

void expect(bool cond, const char* expr, int line) {
  if (!cond) {
    std::cerr << "FAIL line " << line << ": " << expr << std::endl;
    ++failures;
  }
}

#define EXPECT(cond) expect((cond), #cond, __LINE__)

}  // namespace

int main() {
  ThreadPool pool;
  pool.SetLogEnabled(false);
  pool.Start(4);

  // 1. 基本 submit：返回值与参数
  auto fut = pool.submit([](int a, int b) { return a + b; }, 10, 20);
  EXPECT(fut.get() == 30);

  // 2. 并发 submit：500 个任务，结果全部正确
  constexpr int kTasks = 500;
  std::vector<std::future<int>> futures;
  futures.reserve(kTasks);
  for (int i = 0; i < kTasks; ++i) {
    futures.push_back(pool.submit([i] { return i * i; }));
  }
  bool allCorrect = true;
  for (int i = 0; i < kTasks; ++i) {
    allCorrect = allCorrect && futures[i].get() == i * i;
  }
  EXPECT(allCorrect);

  // 3. 异常通过 future 传播
  auto bad = pool.submit([]() -> int {
    throw std::runtime_error("boom");
    return 0;
  });
  bool threw = false;
  try {
    (void)bad.get();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  EXPECT(threw);

  // 4. 回调模式：任务在工作线程执行，runPendingCallbacks 拉取回调
  int cbResult = 0;
  pool.submitWithCallback([](int x) { return x * 10; },
                          [&cbResult](int v) { cbResult = v; }, 7);
  for (int i = 0; i < 2000 && cbResult == 0; ++i) {
    pool.runPendingCallbacks();
    std::this_thread::yield();
  }
  EXPECT(cbResult == 70);

  // 5. 单线程下优先级顺序：HIGH -> NORMAL -> LOW
  {
    ThreadPool single;
    single.SetLogEnabled(false);
    single.Start(1);
    std::vector<int> order;
    std::mutex orderMutex;
    std::vector<std::future<void>> fs;
    fs.push_back(single.submitWithPriority(TaskPriority::kLow, [&] {
      std::lock_guard<std::mutex> lk(orderMutex);
      order.push_back(2);
    }));
    fs.push_back(single.submitWithPriority(TaskPriority::kHigh, [&] {
      std::lock_guard<std::mutex> lk(orderMutex);
      order.push_back(0);
    }));
    fs.push_back(single.submit([&] {
      std::lock_guard<std::mutex> lk(orderMutex);
      order.push_back(1);
    }));
    for (auto& f : fs) {
      f.get();
    }
    EXPECT(order.size() == 3);
    EXPECT(order.size() == 3 && order[0] == 0 && order[1] == 1 && order[2] == 2);
  }

  if (failures == 0) {
    std::cout << "threadpool_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "threadpool_test: " << failures << " failure(s)" << std::endl;
  return 1;
}

