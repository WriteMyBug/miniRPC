#include "minirpc/rpc/LoadBalancer.h"

namespace minirpc {

uint64_t ConsistentHash::hash64(const std::string& s) {
  // libstdc++ 的 std::hash<string> 基于 _Hash_bytes（类 Murmur），雪崩性优于 FNV-1a。
  return static_cast<uint64_t>(std::hash<std::string>{}(s));
}

ConsistentHash::ConsistentHash(size_t virtualNodesPerNode)
    : virtualNodesPerNode_(virtualNodesPerNode) {}

void ConsistentHash::setNodes(const std::vector<std::string>& nodeKeys) {
  ring_.clear();
  nodeKeys_ = nodeKeys;
  for (const std::string& key : nodeKeys) {
    for (size_t i = 0; i < virtualNodesPerNode_; ++i) {
      const uint64_t h = hash64(key + "#" + std::to_string(i));
      ring_[h] = key;
    }
  }
}

std::string ConsistentHash::selectNode(const std::string& key) const {
  if (ring_.empty()) {
    return {};
  }
  const uint64_t h = hash64(key);
  const auto it = ring_.lower_bound(h);
  if (it == ring_.end()) {
    return ring_.begin()->second;
  }
  return it->second;
}

}  // namespace minirpc
