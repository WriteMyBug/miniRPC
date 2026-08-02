#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace minirpc {

// 自定义协议头，固定 24 字节，多字节字段在网络字节序（大端）传输。
// 布局：魔数(4) | 版本(1) | 类型(1) | 标志(2) | 序列号(8) | body长度(4) | 保留(4)
struct ProtocolHeader {
  uint32_t magic = 0;     // 魔数：0x4D494E52（"MINR"）
  uint8_t version = 0;    // 协议版本
  uint8_t type = 0;       // 1=请求，2=响应
  uint16_t flags = 0;     // 保留标志（预留压缩位等）
  uint64_t seq = 0;       // 序列号（客户端生成，超时重试去重用）
  uint32_t bodyLen = 0;   // body（protobuf 序列化字节）长度
  uint32_t reserved = 0;  // 保留，填 0
};
static_assert(sizeof(ProtocolHeader) == 24, "ProtocolHeader must be 24 bytes");

enum class MessageType : uint8_t {
  kRequest = 1,
  kResponse = 2,
};

constexpr uint32_t kMagic = 0x4D494E52u;  // "MINR"
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 24;
constexpr size_t kMaxBodySize = 64 * 1024 * 1024;  // 64MB 上限，防恶意长度

// 一条完整报文：头部 + body（body 为 protobuf 序列化后的字节串）。
struct ProtocolMessage {
  ProtocolHeader header{};
  std::string body;
};

}  // namespace minirpc

