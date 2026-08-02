#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "minirpc/rpc/LoadBalancer.h"

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
  // 1. 轮询：按序循环
  RoundRobin rr;
  rr.setNodeCount(3);
  EXPECT(rr.next() == 0);
  EXPECT(rr.next() == 1);
  EXPECT(rr.next() == 2);
  EXPECT(rr.next() == 0);
  rr.setNodeCount(2);
  EXPECT(rr.next() == 0);
  EXPECT(rr.next() == 1);

  // 2. 一致性哈希：同一 key 稳定落在同一节点
  ConsistentHash ch(100);
  ch.setNodes({"a", "b", "c"});
  EXPECT(!ch.selectNode("key").empty());
  EXPECT(ch.selectNode("key") == ch.selectNode("key"));

  // 3. 分布性：200 个不同 key 覆盖全部节点
  std::set<std::string> used;
  for (int i = 0; i < 200; ++i) {
    used.insert(ch.selectNode("key-" + std::to_string(i)));
  }
  EXPECT(used.count("a") > 0);
  EXPECT(used.count("b") > 0);
  EXPECT(used.count("c") > 0);

  // 4. 移除节点：原本落在 a/b 的 key 不变，落在 c 的 key 迁移到 a/b
  bool stable = true;
  std::set<std::string> migrated;
  for (int i = 0; i < 500; ++i) {
    const std::string key = "key-" + std::to_string(i);
    const std::string before = ch.selectNode(key);
    ch.setNodes({"a", "b"});
    const std::string after = ch.selectNode(key);
    ch.setNodes({"a", "b", "c"});
    if (before == "c") {
      migrated.insert(after);
      if (after != "a" && after != "b") {
        stable = false;
      }
    } else {
      if (after != before) {
        stable = false;
      }
    }
  }
  EXPECT(stable);
  EXPECT(!migrated.empty());
  EXPECT(ch.nodeCount() == 3);

  if (failures == 0) {
    std::cout << "loadbalancer_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "loadbalancer_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
