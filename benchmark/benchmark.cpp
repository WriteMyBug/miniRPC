#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "minirpc/common/Logger.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/RpcChannel.h"
#include "minirpc/rpc/RpcController.h"

#include "echo.pb.h"

using namespace minirpc;
using Clock = std::chrono::steady_clock;

// 用法：benchmark [ip] [port] [clients] [calls_per_client] [out_csv]
// 每个客户端线程独立 RpcChannel，连续调用 EchoService，统计 QPS 与延迟分位。
int main(int argc, char* argv[]) {
  const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
  const uint16_t port =
      (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 8888;
  const int clients = (argc > 3) ? std::atoi(argv[3]) : 10;
  const int callsPerClient = (argc > 4) ? std::atoi(argv[4]) : 1000;
  const char* outCsv = (argc > 5) ? argv[5] : nullptr;
  Logger::instance().setLogLevel(LogLevel::kWarn);

  std::vector<std::vector<long>> latencies(clients);
  std::atomic<long> failed{0};

  auto worker = [&](int id) {
    RpcChannel channel(InetAddress(ip, port), 5000, 0);
    minirpc::example::EchoService::Stub stub(&channel);
    latencies[id].reserve(callsPerClient);
    for (int i = 0; i < callsPerClient; ++i) {
      RpcController ctrl;
      minirpc::example::EchoRequest req;
      req.set_message("benchmark");
      minirpc::example::EchoResponse resp;
      const auto t0 = Clock::now();
      stub.Echo(&ctrl, &req, &resp, nullptr);
      const long us = std::chrono::duration_cast<std::chrono::microseconds>(
                          Clock::now() - t0)
                          .count();
      latencies[id].push_back(us);
      if (ctrl.Failed()) {
        failed.fetch_add(1);
      }
    }
  };

  std::vector<std::thread> threads;
  const auto start = Clock::now();
  for (int i = 0; i < clients; ++i) {
    threads.emplace_back(worker, i);
  }
  for (auto& t : threads) {
    t.join();
  }
  const long elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             Clock::now() - start)
                             .count();

  std::vector<long> all;
  long sum = 0;
  for (const auto& v : latencies) {
    for (long us : v) {
      all.push_back(us);
      sum += us;
    }
  }
  std::sort(all.begin(), all.end());
  const auto pct = [&all](double p) -> long {
    if (all.empty()) {
      return 0;
    }
    return all[std::min<size_t>(all.size() - 1,
                                static_cast<size_t>(all.size() * p))];
  };
  const double elapsedSec = elapsedUs / 1e6;
  const long qps =
      elapsedSec > 0 ? static_cast<long>(all.size() / elapsedSec) : 0;
  const long avgUs = all.empty() ? 0 : static_cast<long>(sum / all.size());
  const long maxUs = all.empty() ? 0 : all.back();

  std::cout << "benchmark: clients=" << clients << " total=" << all.size()
            << " elapsed_ms=" << elapsedUs / 1000 << " QPS=" << qps
            << " avg_us=" << avgUs << " p50_us=" << pct(0.50)
            << " p90_us=" << pct(0.90) << " p99_us=" << pct(0.99)
            << " max_us=" << maxUs << " failed=" << failed.load() << std::endl;

  if (outCsv != nullptr) {
    std::ofstream f(outCsv, std::ios::app);
    f << clients << "," << all.size() << "," << qps << "," << avgUs << ","
      << pct(0.50) << "," << pct(0.99) << "," << maxUs << ","
      << failed.load() << "\n";
  }
  return failed.load() == 0 ? 0 : 1;
}

