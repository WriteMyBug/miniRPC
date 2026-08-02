#pragma once

#include <cstddef>
#include <string>
#include <sys/types.h>
#include <vector>

namespace minirpc {

// 可增长字节缓冲，带读/写游标，用于 TCP 粘包/半包处理。
class Buffer {
 public:
  static constexpr size_t kInitialSize = 1024;
  static constexpr size_t kCheapPrepend = 8;

  explicit Buffer(size_t initialSize = kInitialSize);

  size_t readableBytes() const { return writerIndex_ - readerIndex_; }
  size_t writableBytes() const { return buffer_.size() - writerIndex_; }
  size_t prependableBytes() const { return readerIndex_; }

  const char* peek() const { return begin() + readerIndex_; }
  const char* beginWrite() const { return begin() + writerIndex_; }
  char* beginWrite() { return begin() + writerIndex_; }

  void retrieve(size_t len);
  void retrieveAll();
  std::string retrieveAllAsString();
  std::string retrieveAsString(size_t len);

  void append(const char* data, size_t len);
  void append(const std::string& data) { append(data.data(), data.size()); }

  // 从 fd 读入数据（readv 一次尽量读完），返回读取字节数，出错时写入 savedErrno。
  ssize_t readFd(int fd, int* savedErrno);

 private:
  char* begin() { return buffer_.data(); }
  const char* begin() const { return buffer_.data(); }

  void ensureWritableBytes(size_t len);

  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;
};

}  // namespace minirpc

