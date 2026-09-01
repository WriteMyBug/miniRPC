#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/RpcChannel.h"
#include "minirpc/rpc/RpcController.h"
#include "minirpc/rpc/RpcServer.h"

#include "calculator.pb.h"
#include "echo.pb.h"

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

class TestEchoService : public minirpc::example::EchoService {
 public:
  void Echo(google::protobuf::RpcController* controller,
            const minirpc::example::EchoRequest* request,
            minirpc::example::EchoResponse* response,
            google::protobuf::Closure* done) override {
    (void)controller;
    response->set_message("echo:" + request->message());
    if (done != nullptr) {
      done->Run();
    }
  }
};

class TestCalcService : public minirpc::example::CalculatorService {
 public:
  void Calc(google::protobuf::RpcController* controller,
            const minirpc::example::CalcRequest* request,
            minirpc::example::CalcResponse* response,
            google::protobuf::Closure* done) override {
    (void)controller;
    response->set_result(request->a() * request->b());
    response->set_server_id("multi");
    if (done != nullptr) {
      done->Run();
    }
  }
};

}  // namespace

// 多 Reactor 正确性：ioThreads=2 的 RpcServer，8 客户端线程并发调用，全部正确。
int main() {
  Logger::instance().setLogLevel(LogLevel::kWarn);
  constexpr uint16_t kPort = 18950;

  TestEchoService echo;
  TestCalcService calc;

  std::promise<std::shared_ptr<EventLoop>> ready;
  std::thread serverThread([&] {
    auto loop = std::make_shared<EventLoop>();
    RpcServer server(loop.get(), InetAddress(kPort), "multi", /*ioThreads=*/2);
    server.registerService(&echo);
    server.registerService(&calc);
    server.start();
    ready.set_value(loop);
    loop->loop();
  });
  auto loop = ready.get_future().get();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // 1. 并发 echo：8 线程 x 200 次，校验回显一致。
  {
    std::atomic<int> failed{0};
    std::vector<std::thread> clients;
    for (int c = 0; c < 8; ++c) {
      clients.emplace_back([&, c] {
        RpcChannel channel(InetAddress("127.0.0.1", kPort), 2000, 1);
        minirpc::example::EchoService::Stub stub(&channel);
        for (int i = 0; i < 200; ++i) {
          RpcController ctrl;
          minirpc::example::EchoRequest req;
          req.set_message("c" + std::to_string(c) + "-" + std::to_string(i));
          minirpc::example::EchoResponse resp;
          stub.Echo(&ctrl, &req, &resp, nullptr);
          if (ctrl.Failed() || resp.message() != "echo:" + req.message()) {
            failed.fetch_add(1);
          }
        }
      });
    }
    for (auto& t : clients) {
      t.join();
    }
    EXPECT(failed.load() == 0);
  }

  // 2. calculator 正常调用（走同一多 reactor 服务端）。
  {
    RpcChannel channel(InetAddress("127.0.0.1", kPort), 2000, 1);
    minirpc::example::CalculatorService::Stub stub(&channel);
    RpcController ctrl;
    minirpc::example::CalcRequest req;
    req.set_a(6);
    req.set_b(7);
    req.set_op("*");
    minirpc::example::CalcResponse resp;
    stub.Calc(&ctrl, &req, &resp, nullptr);
    EXPECT(!ctrl.Failed());
    EXPECT(resp.result() == 42);
    EXPECT(resp.server_id() == "multi");
  }

  loop->quit();
  serverThread.join();

  if (failures == 0) {
    std::cout << "multireactor_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "multireactor_test: " << failures << " failure(s)" << std::endl;
  return 1;
}

