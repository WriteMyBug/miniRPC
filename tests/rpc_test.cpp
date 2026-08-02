#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/net/TcpServer.h"
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
    if (request->op() == "+") {
      response->set_result(request->a() + request->b());
    } else if (request->op() == "/" && request->b() == 0) {
      controller->SetFailed("division by zero");
    } else {
      response->set_result(request->a() * request->b());
    }
    if (done != nullptr) {
      done->Run();
    }
  }
};

}  // namespace

int main() {
  Logger::instance().setLogLevel(LogLevel::kWarn);

  constexpr uint16_t kRpcPort = 18888;
  constexpr uint16_t kSinkPort = 18889;

  TestEchoService echo;
  TestCalcService calc;

  // 启动 RPC 服务端（EventLoop 必须在服务线程内创建）
  std::promise<std::shared_ptr<EventLoop>> loopPromise;
  std::thread serverThread([&] {
    auto loop = std::make_shared<EventLoop>();
    RpcServer server(loop.get(), InetAddress(kRpcPort), "test-rpc");
    server.registerService(&echo);
    server.registerService(&calc);
    server.start();
    loopPromise.set_value(loop);  // 监听就绪后再通知主线程
    loop->loop();
  });
  auto serverLoop = loopPromise.get_future().get();

  // 启动"黑洞"服务端：接受连接但不回包，用于超时测试
  std::promise<std::shared_ptr<EventLoop>> sinkPromise;
  std::thread sinkThread([&] {
    auto loop = std::make_shared<EventLoop>();
    TcpServer sink(loop.get(), InetAddress(kSinkPort), "sink");
    sink.start();
    sinkPromise.set_value(loop);
    loop->loop();
  });
  auto sinkLoop = sinkPromise.get_future().get();

  RpcChannel channel(InetAddress("127.0.0.1", kRpcPort), 2000, 2);
  minirpc::example::CalculatorService::Stub calcStub(&channel);
  minirpc::example::EchoService::Stub echoStub(&channel);

  // 1. 正常调用：6 * 7 = 42
  {
    RpcController ctrl;
    minirpc::example::CalcRequest req;
    req.set_a(6);
    req.set_b(7);
    req.set_op("*");
    minirpc::example::CalcResponse resp;
    calcStub.Calc(&ctrl, &req, &resp, nullptr);
    EXPECT(!ctrl.Failed());
    EXPECT(resp.result() == 42);
  }

  // 2. 服务端业务错误：除零
  {
    RpcController ctrl;
    minirpc::example::CalcRequest req;
    req.set_a(1);
    req.set_b(0);
    req.set_op("/");
    minirpc::example::CalcResponse resp;
    calcStub.Calc(&ctrl, &req, &resp, nullptr);
    EXPECT(ctrl.Failed());
  }

  // 3. echo 回环
  {
    RpcController ctrl;
    minirpc::example::EchoRequest req;
    req.set_message("ping");
    minirpc::example::EchoResponse resp;
    echoStub.Echo(&ctrl, &req, &resp, nullptr);
    EXPECT(!ctrl.Failed());
    EXPECT(resp.message() == "echo:ping");
  }

  // 4. 超时重试：黑洞服务，300ms 超时 + 1 次重试，最终 kTimeout 且耗时 >= 500ms
  {
    RpcChannel sinkChannel(InetAddress("127.0.0.1", kSinkPort), 300, 1);
    minirpc::example::EchoService::Stub sinkStub(&sinkChannel);
    RpcController ctrl;
    minirpc::example::EchoRequest req;
    req.set_message("x");
    minirpc::example::EchoResponse resp;
    const auto start = std::chrono::steady_clock::now();
    sinkStub.Echo(&ctrl, &req, &resp, nullptr);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count();
    EXPECT(ctrl.Failed());
    EXPECT(ctrl.errorCode() == ErrorCode::kTimeout);
    EXPECT(elapsedMs >= 500);
  }

  // 5. 连接拒绝（端口 1 无服务监听）
  {
    RpcChannel bad(InetAddress("127.0.0.1", 1), 300, 0);
    minirpc::example::EchoService::Stub badStub(&bad);
    RpcController ctrl;
    minirpc::example::EchoRequest req;
    req.set_message("x");
    minirpc::example::EchoResponse resp;
    badStub.Echo(&ctrl, &req, &resp, nullptr);
    EXPECT(ctrl.Failed());
  }

  serverLoop->quit();
  sinkLoop->quit();
  serverThread.join();
  sinkThread.join();

  if (failures == 0) {
    std::cout << "rpc_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "rpc_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
