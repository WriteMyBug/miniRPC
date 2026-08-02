#include <arpa/inet.h>

#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "minirpc/codec/Codec.h"
#include "minirpc/codec/Protocol.h"
#include "minirpc/net/Buffer.h"

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

ProtocolMessage makeMessage(uint64_t seq, MessageType type,
                            const std::string& body) {
  ProtocolMessage msg;
  msg.header.magic = kMagic;
  msg.header.version = kProtocolVersion;
  msg.header.type = static_cast<uint8_t>(type);
  msg.header.seq = seq;
  msg.body = body;
  return msg;
}

void appendU32(Buffer* b, uint32_t v) {
  const uint32_t be = htonl(v);
  b->append(reinterpret_cast<const char*>(&be), sizeof(be));
}

}  // namespace

int main() {
  static_assert(kHeaderSize == 24, "header size must be 24");

  // 1. 编码 -> 解码回环
  Buffer wire;
  Codec::encode(makeMessage(42, MessageType::kRequest, "hello-protobuf"),
                &wire);
  auto msg = Codec::tryDecode(&wire);
  EXPECT(msg.has_value());
  EXPECT(msg->header.seq == 42);
  EXPECT(msg->header.type == static_cast<uint8_t>(MessageType::kRequest));
  EXPECT(msg->body == "hello-protobuf");
  EXPECT(wire.readableBytes() == 0);

  // 2. 半包：逐字节喂入，只有全部到达才能解析
  Buffer full;
  Codec::encode(makeMessage(7, MessageType::kResponse, "12345"), &full);
  const std::string bytes(full.peek(), full.readableBytes());
  Buffer fragmented;
  bool decodedOnlyAtEnd = true;
  for (size_t i = 0; i < bytes.size(); ++i) {
    fragmented.append(bytes.data() + i, 1);
    const bool lastByte = (i + 1 == bytes.size());
    auto part = Codec::tryDecode(&fragmented);
    if (lastByte) {
      decodedOnlyAtEnd = decodedOnlyAtEnd && part.has_value() &&
                         part->body == "12345";
    } else {
      decodedOnlyAtEnd = decodedOnlyAtEnd && !part.has_value();
    }
  }
  EXPECT(decodedOnlyAtEnd);

  // 3. 粘包：两条报文一次到达，可连续解析
  Buffer sticky;
  Codec::encode(makeMessage(1, MessageType::kRequest, "AAAA"), &sticky);
  Codec::encode(makeMessage(2, MessageType::kResponse, "BBBB"), &sticky);
  auto first = Codec::tryDecode(&sticky);
  auto second = Codec::tryDecode(&sticky);
  EXPECT(first.has_value() && first->header.seq == 1 && first->body == "AAAA");
  EXPECT(second.has_value() && second->header.seq == 2 && second->body == "BBBB");
  EXPECT(sticky.readableBytes() == 0);

  // 4. 任意切分位置：先喂前 k 字节再喂剩余，仍能完整解析
  Buffer whole;
  Codec::encode(makeMessage(99, MessageType::kRequest, "split-me-anywhere"),
                &whole);
  const std::string data(whole.peek(), whole.readableBytes());
  bool allSplitsOk = true;
  for (size_t k = 0; k < data.size(); ++k) {
    Buffer split;
    split.append(data.data(), k);
    auto before = Codec::tryDecode(&split);
    split.append(data.data() + k, data.size() - k);
    auto after = Codec::tryDecode(&split);
    allSplitsOk = allSplitsOk && !before.has_value() && after.has_value() &&
                  after->body == "split-me-anywhere";
  }
  Buffer wholeAtOnce;
  wholeAtOnce.append(data);
  auto atOnce = Codec::tryDecode(&wholeAtOnce);
  allSplitsOk = allSplitsOk && atOnce.has_value() &&
                atOnce->body == "split-me-anywhere";
  EXPECT(allSplitsOk);

  // 5. 非法魔数：抛异常
  Buffer bad;
  bad.append("\x01\x02\x03\x04", 4);
  for (int i = 4; i < 24; ++i) {
    bad.append("x", 1);
  }
  bool threwBadMagic = false;
  try {
    (void)Codec::tryDecode(&bad);
  } catch (const std::runtime_error&) {
    threwBadMagic = true;
  }
  EXPECT(threwBadMagic);

  // 6. 超长 bodyLen：抛异常
  Buffer huge;
  appendU32(&huge, kMagic);
  huge.append("\x01\x01", 2);  // version=1, type=1
  huge.append("\x00\x00", 2);  // flags=0
  appendU32(&huge, 0);         // seq 高 4 字节
  appendU32(&huge, 1);         // seq 低 4 字节 = 1
  appendU32(&huge, kMaxBodySize + 1);
  appendU32(&huge, 0);  // reserved
  bool threwHuge = false;
  try {
    (void)Codec::tryDecode(&huge);
  } catch (const std::runtime_error&) {
    threwHuge = true;
  }
  EXPECT(threwHuge);

  if (failures == 0) {
    std::cout << "codec_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "codec_test: " << failures << " failure(s)" << std::endl;
  return 1;
}
