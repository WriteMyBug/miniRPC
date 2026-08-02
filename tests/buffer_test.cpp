#include <unistd.h>

#include <iostream>
#include <string>

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

}  // namespace

int main() {
  // 基础读写
  Buffer buf;
  EXPECT(buf.readableBytes() == 0);
  buf.append("hello", 5);
  EXPECT(buf.readableBytes() == 5);
  EXPECT(std::string(buf.peek(), 5) == "hello");
  EXPECT(buf.retrieveAsString(2) == "he");
  EXPECT(buf.readableBytes() == 3);
  EXPECT(buf.retrieveAllAsString() == "llo");
  EXPECT(buf.readableBytes() == 0);

  // 半包模拟：分片到达后按边界消费
  Buffer msg;
  msg.append("1234567890", 10);
  EXPECT(std::string(msg.peek(), 4) == "1234");
  msg.retrieve(4);
  EXPECT(msg.retrieveAllAsString() == "567890");

  // 粘包模拟：两个包一次到达，按长度各取一半
  Buffer two;
  two.append("AAAA", 4);
  two.append("BBBB", 4);
  EXPECT(two.retrieveAsString(4) == "AAAA");
  EXPECT(two.retrieveAsString(4) == "BBBB");
  EXPECT(two.readableBytes() == 0);

  // 大块数据触发扩容
  const std::string big(100000, 'x');
  Buffer bigBuf;
  bigBuf.append(big.data(), big.size());
  EXPECT(bigBuf.readableBytes() == big.size());
  EXPECT(bigBuf.retrieveAllAsString() == big);

  // readFd：从管道读取数据
  int fds[2] = {-1, -1};
  EXPECT(::pipe(fds) == 0);
  const std::string payload = "abcdef";
  EXPECT(::write(fds[1], payload.data(), payload.size()) ==
         static_cast<ssize_t>(payload.size()));
  Buffer pipeBuf;
  int savedErrno = 0;
  const ssize_t n = pipeBuf.readFd(fds[0], &savedErrno);
  EXPECT(n == static_cast<ssize_t>(payload.size()));
  EXPECT(pipeBuf.retrieveAllAsString() == payload);
  ::close(fds[0]);
  ::close(fds[1]);

  if (failures == 0) {
    std::cout << "buffer_test: all tests passed" << std::endl;
    return 0;
  }
  std::cerr << "buffer_test: " << failures << " failure(s)" << std::endl;
  return 1;
}

