#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace minirpc {

// 轮询负载均衡：按调用次序依次选择节点。
class RoundRobin {
 public:
  void setNodeCount(size_t n) { nodeCount_ = n; }
  size_t next() {
    if (nodeCount_ == 0) {
      return 0;
    }
    return index_.fetch_add(1) % nodeCount_;
  }

 private:
  std::atomic<size_t> index_{0};
  size_t nodeCount_ = 0;
};

// 一致性哈希：虚拟节点 + 有序环（FNV-1a 哈希），同一 key 稳定落在同一节点。
class ConsistentHash {
 public:
  explicit ConsistentHash(size_t virtualNodesPerNode = 100);

  void setNodes(const std::vector<std::string>& nodeKeys);
  std::string selectNode(const std::string& key) const;
  size_t nodeCount() const { return nodeKeys_.size(); }

 private:
  static uint64_t hash64(const std::string& s);

  std::map<uint64_t, std::string> ring_;
  size_t virtualNodesPerNode_;
  std::vector<std::string> nodeKeys_;
};

}  // namespace minirpc
