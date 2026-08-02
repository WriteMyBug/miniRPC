#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/net/InetAddress.h"
#include "minirpc/registry/RegistryServiceImpl.h"
#include "minirpc/rpc/LoadBalanceChannel.h"
#include "minirpc/rpc/RpcChannel.h"
#include "minirpc/rpc/RpcController.h"
#include "minirpc/rpc/RpcServer.h"
#include "minirpc/rpc/RpcServiceNode.h"

#include "calculator.pb.h"
#include "registry.pb.h"

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

class NamedCalcService : public minirpc::example::CalculatorService {
 public:
  explicit NamedCalcService(std::string name) : name_(std::move(name)) {}

  void Calc(google::protobuf::RpcController* controller,
            const minirpc::example::CalcRequest* request,
            minirpc::example::CalcResponse* response,
            google::protobuf::Closure* done) override {
    (void)controller;
    response->set_result(request->a() + request->b());
    response->set_server_id(name_);
    if (done != nullptr) {
      done->Run();
    }
  }

 private:
  std::string name_;
};

struct NodeHandle {
  std::promise<std::pair<std::shared_ptr<EventLoop>, RpcServiceNode*>> ready;
  std::thread thread;
};

void startNode(NodeHandle& h, uint16_t port, std::string name,
               InetAddress registryAddr, NamedCalcService& service) {
  // port/name/registryAddr 按值捕获：线程可能在函数返回后才读取参数。
  h.thread = std::thread([&, port, name, registryAddr] {
    auto loop = std::make_shared<EventLoop>();
    auto* node = new RpcServiceNode(
        loop.get(), InetAddress(port), name, registryAddr,
        "minirpc.example.CalculatorService",
        /*heartbeatIntervalMs=*/300, /*heartbeatTimeoutMs=*/1200);
    node->registerService(&service);
    node->start();
    h.ready.set_value({loop, node});
    loop->loop();
    delete node;
  });
}

}  // namespace

int main() {
  Logger::instance().setLogLevel(LogLevel::kWarn);
  constexpr uint16_t kRegPort = 18900;
  constexpr uint16_t kPortA = 18901;
  constexpr uint16_t kPortB = 18902;
  const std::string kService = "minirpc.example.CalculatorService";

  // 注册中心
  std::promise<std::shared_ptr<EventLoop>> regReady;
  std::thread regThread([&] {
    auto loop = std::make_shared<EventLoop>();
    RpcServer server(loop.get(), InetAddress(kRegPort), "registry");
    RegistryServiceImpl registry(std::chrono::milliseconds(1200));
    server.registerService(&registry);
    server.start();
    regReady.set_value(loop);
    loop->loop();
  });
  auto regLoop = regReady.get_future().get();

  // 双服务节点
  NamedCalcService calcA("node-a");
  NamedCalcService calcB("node-b");
  NodeHandle nodeA;
  NodeHandle nodeB;
  startNode(nodeA, kPortA, "node-a", InetAddress("127.0.0.1", kRegPort),
            calcA);
  startNode(nodeB, kPortB, "node-b", InetAddress("127.0.0.1", kRegPort),
            calcB);
  auto nodeAInfo = nodeA.ready.get_future().get();
  auto nodeBInfo = nodeB.ready.get_future().get();
  auto loopA = nodeAInfo.first;
  auto loopB = nodeBInfo.first;
  auto nodeBPtr = nodeBInfo.second;
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  // 1. 轮询负载均衡：12 次调用覆盖两个节点
  {
    LoadBalanceChannel lb(InetAddress("127.0.0.1", kRegPort), kService,
                          LoadBalanceChannel::BalanceType::kRoundRobin,
                          /*refreshIntervalMs=*/500, /*callTimeoutMs=*/1000,
                          /*callRetries=*/0);
    minirpc::example::CalculatorService::Stub stub(&lb);
    std::map<std::string, int> counts;
    bool allOk = true;
    for (int i = 0; i < 12; ++i) {
      RpcController ctrl;
      minirpc::example::CalcRequest req;
      req.set_a(i);
      req.set_b(1);
      req.set_op("+");
      minirpc::example::CalcResponse resp;
      stub.Calc(&ctrl, &req, &resp, nullptr);
      if (ctrl.Failed()) {
        allOk = false;
        continue;
      }
      counts[resp.server_id()]++;
    }
    EXPECT(allOk);
    EXPECT(counts["node-a"] > 0 && counts["node-b"] > 0);
    EXPECT(counts["node-a"] + counts["node-b"] == 12);
    EXPECT(lb.nodeCount() == 2);
  }

  // 2. 一致性哈希：相同请求稳定落在同一节点
  {
    LoadBalanceChannel lb(InetAddress("127.0.0.1", kRegPort), kService,
                          LoadBalanceChannel::BalanceType::kConsistentHash,
                          500, 1000, 0);
    minirpc::example::CalculatorService::Stub stub(&lb);
    RpcController c1;
    RpcController c2;
    minirpc::example::CalcRequest req1;
    req1.set_a(1);
    req1.set_b(2);
    req1.set_op("+");
    minirpc::example::CalcRequest req2;
    req2.set_a(1);
    req2.set_b(2);
    req2.set_op("+");
    minirpc::example::CalcResponse r1;
    minirpc::example::CalcResponse r2;
    stub.Calc(&c1, &req1, &r1, nullptr);
    stub.Calc(&c2, &req2, &r2, nullptr);
    EXPECT(!c1.Failed() && !c2.Failed());
    EXPECT(!r1.server_id().empty());
    EXPECT(r1.server_id() == r2.server_id());
  }

  // 3. 故障剔除：节点 B 停止心跳（不注销），注册中心超时后将其剔除
  nodeBPtr->stopHeartbeat();
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));

  {
    RpcChannel regChannel(InetAddress("127.0.0.1", kRegPort), 1000, 1);
    minirpc::registry::RegistryService::Stub regStub(&regChannel);
    RpcController ctrl;
    minirpc::registry::DiscoverRequest req;
    req.set_service_name(kService);
    minirpc::registry::DiscoverResponse resp;
    regStub.Discover(&ctrl, &req, &resp, nullptr);
    EXPECT(!ctrl.Failed());
    EXPECT(resp.nodes_size() == 1);
    EXPECT(resp.nodes_size() == 1 && resp.nodes(0).name() == "node-a");
  }
  {
    LoadBalanceChannel lb(InetAddress("127.0.0.1", kRegPort), kService,
                          LoadBalanceChannel::BalanceType::kRoundRobin,
                          500, 1000, 0);
    minirpc::example::CalculatorService::Stub stub(&lb);
    std::string lastServer;
    bool allOk = true;
    for (int i = 0; i < 6; ++i) {
      RpcController ctrl;
      minirpc::example::CalcRequest req;
      req.set_a(i);
      req.set_b(i);
      req.set_op("+");
      minirpc::example::CalcResponse resp;
      stub.Calc(&ctrl, &req, &resp, nullptr);
      if (ctrl.Failed()) {
        allOk = false;
        break;
      }
      lastServer = resp.server_id();
    }
    EXPECT(allOk);
    EXPECT(lb.nodeCount() == 1);
    EXPECT(lastServer == "node-a");
  }

  loopA->quit();
  loopB->quit();
  regLoop->quit();
  nodeA.thread.join();
  nodeB.thread.join();
  regThread.join();

  if (failures == 0) {
    std::cout << "registry_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "registry_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
