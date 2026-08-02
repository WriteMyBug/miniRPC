#include <cstdlib>
#include <iostream>
#include <string>

#include "minirpc/common/ErrorCode.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/RpcChannel.h"
#include "minirpc/rpc/RpcController.h"

#include "calculator.pb.h"
#include "echo.pb.h"

using namespace minirpc;

// 用法：calculator_client [ip] [port]
int main(int argc, char* argv[]) {
  const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";
  const uint16_t port =
      (argc > 2) ? static_cast<uint16_t>(std::atoi(argv[2])) : 8888;

  RpcChannel channel(InetAddress(ip, port), /*timeoutMs=*/2000,
                     /*maxRetries=*/2);
  minirpc::example::CalculatorService::Stub calcStub(&channel);
  minirpc::example::EchoService::Stub echoStub(&channel);

  auto runCalc = [&](double a, double b, const std::string& op) {
    RpcController ctrl;
    minirpc::example::CalcRequest req;
    req.set_a(a);
    req.set_b(b);
    req.set_op(op);
    minirpc::example::CalcResponse resp;
    calcStub.Calc(&ctrl, &req, &resp, nullptr);
    if (ctrl.Failed()) {
      std::cout << a << " " << op << " " << b << " -> ERROR("
                << errorCodeToString(ctrl.errorCode()) << "): "
                << ctrl.ErrorText() << std::endl;
    } else {
      std::cout << a << " " << op << " " << b << " = " << resp.result()
                << std::endl;
    }
  };

  runCalc(1, 2, "+");
  runCalc(7, 6, "*");
  runCalc(10, 4, "-");
  runCalc(9, 3, "/");
  runCalc(1, 0, "/");  // 除零 -> 服务端业务错误
  runCalc(1, 2, "%");  // 未知操作符 -> 服务端业务错误

  RpcController echoCtrl;
  minirpc::example::EchoRequest echoReq;
  echoReq.set_message("hello rpc");
  minirpc::example::EchoResponse echoResp;
  echoStub.Echo(&echoCtrl, &echoReq, &echoResp, nullptr);
  if (echoCtrl.Failed()) {
    std::cout << "echo -> ERROR("
              << errorCodeToString(echoCtrl.errorCode()) << "): "
              << echoCtrl.ErrorText() << std::endl;
    return 1;
  }
  std::cout << "echo -> " << echoResp.message() << std::endl;
  return 0;
}

