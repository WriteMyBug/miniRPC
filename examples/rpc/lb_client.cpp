#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "minirpc/net/InetAddress.h"
#include "minirpc/rpc/LoadBalanceChannel.h"
#include "minirpc/rpc/RpcController.h"

#include "calculator.pb.h"

using namespace minirpc;

// 用法：lb_client [registry_port] [calls] [rr|ch]
int main(int argc, char* argv[]) {
  uint16_t registryPort = 18800;
  int calls = 20;
  std::string mode = "rr";
  if (argc > 1) {
    registryPort = static_cast<uint16_t>(std::atoi(argv[1]));
  }
  if (argc > 2) {
    calls = std::atoi(argv[2]);
  }
  if (argc > 3) {
    mode = argv[3];
  }
  const LoadBalanceChannel::BalanceType type =
      (mode == "ch") ? LoadBalanceChannel::BalanceType::kConsistentHash
                     : LoadBalanceChannel::BalanceType::kRoundRobin;

  LoadBalanceChannel lb(InetAddress("127.0.0.1", registryPort),
                        "minirpc.example.CalculatorService", type,
                        /*refreshIntervalMs=*/1000);
  minirpc::example::CalculatorService::Stub stub(&lb);

  std::map<std::string, int> counts;
  int failed = 0;
  for (int i = 0; i < calls; ++i) {
    RpcController ctrl;
    minirpc::example::CalcRequest req;
    req.set_a(i);
    req.set_b(i + 1);
    req.set_op("+");
    minirpc::example::CalcResponse resp;
    stub.Calc(&ctrl, &req, &resp, nullptr);
    if (ctrl.Failed()) {
      ++failed;
      continue;
    }
    counts[resp.server_id()]++;
  }
  std::cout << "lb_client: mode=" << mode << " nodes=" << lb.nodeCount()
            << " failed=" << failed << std::endl;
  for (const auto& kv : counts) {
    std::cout << "  " << kv.first << ": " << kv.second << std::endl;
  }
  return failed == 0 ? 0 : 1;
}

