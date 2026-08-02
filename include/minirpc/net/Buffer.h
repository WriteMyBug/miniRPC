#pragma once

#include <cstddef>
#include <string>
#include <sys/types.h>
#include <vector>

namespace minirpc {

// 可增长字节缓冲，带读/写两个游标，用于 TCP 粘包/半包处理。
// 设计要点：
//  - retrieve 只移动读游标不删除数据，避免频繁拷贝；
//  - 头部预留 kCheapPrepend 字节，扩容时把未读数据搬到头部；
//  - readFd 用 readv 一次尽量读多，减少系统调用次数。
class Buffer {
 public:
  static constexpr size_t kInitialSize = 1024;
  static constexpr size_t kCheapPrepend = 8;

  explicit Buffer(size_t initialSize = kInitialSize);

  size_t readableBytes() const { return writerIndex_ - readerIndex_; }  // 未读数据长度
  size_t writableBytes() const { return buffer_.size() - writerIndex_; }  // 可写空间
  size_t prependableBytes() const { return readerIndex_; }  // 头部可回收空间

  const char* peek() const { return begin() + readerIndex_; }  // 未读数据起点（只看不取）
  const char* beginWrite() const { return begin() + writerIndex_; }
  char* beginWrite() { return begin() + writerIndex_; }

  void retrieve(size_t len);             // 消费 len 字节（只移游标）
  void retrieveAll();                    // 全部消费，游标回到头部
  std::string retrieveAllAsString();     // 取出全部数据并消费
  std::string retrieveAsString(size_t len);  // 取出 len 字节并消费

  void append(const char* data, size_t len);  // 追加数据（自动扩容）
  void append(const std::string& data) { append(data.data(), data.size()); }

  // 从 fd 读入数据（readv 一次尽量读完），返回读取字节数，出错时写入 savedErrno。
  ssize_t readFd(int fd, int* savedErrno);

 private:
  char* begin() { return buffer_.data(); }
  const char* begin() const { return buffer_.data(); }

  void ensureWritableBytes(size_t len);  // 保证可写空间 >= len（搬移未读数据/扩容）

  std::vector<char> buffer_;   // 底层字节容器
  size_t readerIndex_;         // 读游标：未读数据的起点
  size_t writerIndex_;         // 写游标：可写空间的起点
};

}  // namespace minirpc
