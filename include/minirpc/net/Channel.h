#pragma once

#include <cstdint>
#include <functional>
#include <sys/epoll.h>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class EventLoop;

// 将 fd 与感兴趣的事件、回调绑定；不拥有 fd。
class Channel : public Noncopyable {
 public:
  using EventCallback = std::function<void()>;

  Channel(EventLoop* loop, int fd);
  ~Channel() = default;

  int fd() const { return fd_; }
  uint32_t events() const { return events_; }
  uint32_t revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }
  bool isReading() const { return events_ & kReadEvent; }
  bool isWriting() const { return events_ & kWriteEvent; }

  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }

  void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

  void setRevents(uint32_t revents) { revents_ = revents; }
  void handleEvent();
  void remove();

 private:
  void update();

  static constexpr int kNoneEvent = 0;
  static constexpr int kReadEvent = EPOLLIN;
  static constexpr int kWriteEvent = EPOLLOUT;

  EventLoop* loop_;
  int fd_;
  uint32_t events_ = 0;
  uint32_t revents_ = 0;

  EventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace minirpc
