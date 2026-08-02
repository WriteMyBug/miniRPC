#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include "minirpc/codec/Protocol.h"
#include "minirpc/net/Buffer.h"

namespace minirpc {

// 协议编解码：负责"协议头 + body"的组装与解析。
// - encode：把报文写入 Buffer（TCP 发送缓冲）；
// - tryDecode：从 Buffer 尝试解析一条完整报文，半包返回 nullopt 且不消费数据。
class Codec {
 public:
  static void encode(const ProtocolMessage& msg, Buffer* out);

  // 数据不足（半包）返回 nullopt；解析成功消费对应字节并返回报文；
  // 魔数/版本/长度非法抛出 std::runtime_error。
  static std::optional<ProtocolMessage> tryDecode(Buffer* in);
};

}  // namespace minirpc

