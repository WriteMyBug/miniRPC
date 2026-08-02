#include <csignal>
#include <cstdlib>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/RpcController.h"
#include "minirpc/rpc/RpcServiceNode.h"

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
  explicit CalculatorServiceImpl(std::string nodeName)
      : nodeName_(std::move(nodeName)) {}

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
    response->set_server_id(nodeName_);
    if (done != nullptr) {
      done->Run();
    }
  }

 private:
  std::string nodeName_;
};

// 用法：rpc_node [listen_port] [node_name] [registry_port] [heartbeat_ms]
int main(int argc, char* argv[]) {
  uint16_t port = 9001;
  std::string name = "node";
  uint16_t registryPort = 18800;
  int heartbeatMs = 2000;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  if (argc > 2) {
    name = argv[2];
  }
  if (argc > 3) {
    registryPort = static_cast<uint16_t>(std::atoi(argv[3]));
  }
  if (argc > 4) {
    heartbeatMs = std::atoi(argv[4]);
  }
  Logger::instance().setLogLevel(LogLevel::kInfo);
  std::signal(SIGPIPE, SIG_IGN);

  EventLoop loop;
  RpcServiceNode node(&loop, InetAddress(port), name,
                      InetAddress("127.0.0.1", registryPort),
                      "minirpc.example.CalculatorService", heartbeatMs,
                      heartbeatMs * 3);
  EchoServiceImpl echoService;
  CalculatorServiceImpl calcService(name);
  node.registerService(&echoService);
  node.registerService(&calcService);
  node.start();
  loop.loop();
  return 0;
}
