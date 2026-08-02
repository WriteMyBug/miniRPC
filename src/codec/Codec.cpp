#include "minirpc/codec/Codec.h"

#include <arpa/inet.h>
#include <endian.h>

#include <cstring>

namespace minirpc {

namespace {

void appendU16(Buffer* out, uint16_t v) {
  const uint16_t be = htons(v);
  out->append(reinterpret_cast<const char*>(&be), sizeof(be));
}

void appendU32(Buffer* out, uint32_t v) {
  const uint32_t be = htonl(v);
  out->append(reinterpret_cast<const char*>(&be), sizeof(be));
}

void appendU64(Buffer* out, uint64_t v) {
  const uint64_t be = htobe64(v);
  out->append(reinterpret_cast<const char*>(&be), sizeof(be));
}

uint16_t readU16(const char* p) {
  uint16_t v;
  std::memcpy(&v, p, sizeof(v));
  return ntohs(v);
}

uint32_t readU32(const char* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return ntohl(v);
}

uint64_t readU64(const char* p) {
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return be64toh(v);
}

}  // namespace

void Codec::encode(const ProtocolMessage& msg, Buffer* out) {
  appendU32(out, msg.header.magic);
  out->append(reinterpret_cast<const char*>(&msg.header.version), 1);
  out->append(reinterpret_cast<const char*>(&msg.header.type), 1);
  appendU16(out, msg.header.flags);
  appendU64(out, msg.header.seq);
  appendU32(out, static_cast<uint32_t>(msg.body.size()));
  appendU32(out, msg.header.reserved);
  out->append(msg.body);
}

std::optional<ProtocolMessage> Codec::tryDecode(Buffer* in) {
  // 半包处理核心：数据不够时"只看不拿"（peek），不消费任何字节。
  if (in->readableBytes() < kHeaderSize) {
    return std::nullopt;
  }
  const char* p = in->peek();
  // 魔数不对说明对端发了脏数据/连错端口，直接报错让上层断开。
  if (readU32(p) != kMagic) {
    throw std::runtime_error("Codec: invalid magic number");
  }
  ProtocolMessage msg;
  ProtocolHeader& h = msg.header;
  h.magic = kMagic;
  h.version = static_cast<uint8_t>(p[4]);
  h.type = static_cast<uint8_t>(p[5]);
  h.flags = readU16(p + 6);
  h.seq = readU64(p + 8);
  h.bodyLen = readU32(p + 16);
  h.reserved = readU32(p + 20);

  if (h.version != kProtocolVersion) {
    throw std::runtime_error("Codec: unsupported protocol version");
  }
  if (h.bodyLen > kMaxBodySize) {
    throw std::runtime_error("Codec: body length exceeds limit");
  }
  // 头齐了但 body 没到齐：仍然不消费，等下一次 readFd 攒够。
  if (in->readableBytes() < kHeaderSize + h.bodyLen) {
    return std::nullopt;  // 半包：等剩余数据
  }

  // 数据齐了：先消费头部再取 body，Buffer 游标前移。
  in->retrieve(kHeaderSize);
  msg.body = in->retrieveAsString(h.bodyLen);
  return msg;
}

}  // namespace minirpc
