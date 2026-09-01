#include <csignal>
#include <cstdlib>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/RpcController.h"
#include "minirpc/rpc/RpcServer.h"

#include "calculator.pb.h"
#include "echo.pb.h"

using namespace minirpc;

class EchoServiceImpl : public minirpc::example::EchoService {
 public:
  void Echo(google::protobuf::RpcController* controller,
            const minirpc::example::EchoRequest* request,
            minirpc::example::EchoResponse* response,
            google::protobuf::Closure* done) override {
    (void)controller;
    response->set_message(request->message());
    if (done != nullptr) {
      done->Run();
    }
  }
};

class CalculatorServiceImpl : public minirpc::example::CalculatorService {
 public:
  void Calc(google::protobuf::RpcController* controller,
            const minirpc::example::CalcRequest* request,
            minirpc::example::CalcResponse* response,
            google::protobuf::Closure* done) override {
    const double a = request->a();
    const double b = request->b();
    const std::string& op = request->op();
    auto fail = [controller](const std::string& msg) {
      if (auto* rc = dynamic_cast<RpcController*>(controller)) {
        rc->SetFailed(ErrorCode::kInvalidArgument, msg);
      } else {
        controller->SetFailed(msg);
      }
    };
    if (op == "+") {
      response->set_result(a + b);
    } else if (op == "-") {
      response->set_result(a - b);
    } else if (op == "*") {
      response->set_result(a * b);
    } else if (op == "/") {
      if (b == 0.0) {
        fail("division by zero");
      } else {
        response->set_result(a / b);
      }
    } else {
      fail("unknown operator: " + op);
    }
    if (done != nullptr) {
      done->Run();
    }
  }
};

// 用法：calculator_server [port] [node_name] [pool_threads] [io_threads]
int main(int argc, char* argv[]) {
  uint16_t port = 8888;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  std::string nodeName = "standalone";
  if (argc > 2) {
    nodeName = argv[2];
  }
  size_t poolThreads = 4;
  if (argc > 3) {
    poolThreads = static_cast<size_t>(std::atoi(argv[3]));
  }
  size_t ioThreads = 0;  // 0 = 单 Reactor；>0 = 多 Reactor
  if (argc > 4) {
    ioThreads = static_cast<size_t>(std::atoi(argv[4]));
  }
  Logger::instance().setLogLevel(LogLevel::kInfo);
  std::signal(SIGPIPE, SIG_IGN);

  EventLoop loop;
  RpcServer server(&loop, InetAddress(port), "rpc-server", ioThreads);
  server.setThreadPoolSize(poolThreads);
  EchoServiceImpl echoService;
  CalculatorServiceImpl calcService;
  server.registerService(&echoService);
  server.registerService(&calcService);
  server.start();
  loop.loop();
  return 0;
}
