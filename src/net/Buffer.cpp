#include "minirpc/net/Buffer.h"

#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

namespace minirpc {

Buffer::Buffer(size_t initialSize)
    : buffer_(kCheapPrepend + initialSize),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend) {}

void Buffer::retrieve(size_t len) {
  if (len < readableBytes()) {
    readerIndex_ += len;
  } else {
    retrieveAll();
  }
}

void Buffer::retrieveAll() {
  readerIndex_ = kCheapPrepend;
  writerIndex_ = kCheapPrepend;
}

std::string Buffer::retrieveAllAsString() {
  return retrieveAsString(readableBytes());
}

std::string Buffer::retrieveAsString(size_t len) {
  len = std::min(len, readableBytes());
  std::string result(peek(), len);
  retrieve(len);
  return result;
}

void Buffer::ensureWritableBytes(size_t len) {
  if (writableBytes() >= len) {
    return;
  }
  const size_t readable = readableBytes();
  std::copy(begin() + readerIndex_, begin() + writerIndex_,
            begin() + kCheapPrepend);
  readerIndex_ = kCheapPrepend;
  writerIndex_ = readerIndex_ + readable;
  if (writableBytes() < len) {
    buffer_.resize(writerIndex_ + len);
  }
}

void Buffer::append(const char* data, size_t len) {
  ensureWritableBytes(len);
  std::copy(data, data + len, beginWrite());
  writerIndex_ += len;
}

ssize_t Buffer::readFd(int fd, int* savedErrno) {
  char extrabuf[65536];
  iovec vec[2];
  const size_t writable = writableBytes();
  vec[0].iov_base = beginWrite();
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof(extrabuf);

  const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
  const ssize_t n = ::readv(fd, vec, iovcnt);
  if (n < 0) {
    *savedErrno = errno;
  } else if (static_cast<size_t>(n) <= writable) {
    writerIndex_ += n;
  } else {
    writerIndex_ = buffer_.size();
    append(extrabuf, static_cast<size_t>(n) - writable);
  }
  return n;
}

}  // namespace minirpc

