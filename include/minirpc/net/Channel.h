#pragma once

#include <cstdint>
#include <functional>
#include <sys/epoll.h>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class EventLoop;

// 将 fd 与"感兴趣的事件 + 回调"绑定，是事件分发的最小单元。
// 不拥有 fd（fd 归 Socket/TcpConnection 管），只负责事件状态与回调触发。
class Channel : public Noncopyable {
 public:
  using EventCallback = std::function<void()>;

  Channel(EventLoop* loop, int fd);
  ~Channel() = default;

  int fd() const { return fd_; }             // 关联的 fd
  uint32_t events() const { return events_; }   // 感兴趣的 epoll 事件
  uint32_t revents() const { return revents_; } // epoll_wait 返回的实际事件
  bool isNoneEvent() const { return events_ == kNoneEvent; }  // 已不关心任何事件
  bool isReading() const { return events_ & kReadEvent; }
  bool isWriting() const { return events_ & kWriteEvent; }

  // 下面的 enable/disable 修改 events 后立即 update() 同步到 epoll。
  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }

  // 四种回调：可读 / 可写 / 对端关闭 / 出错。
  void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

  void setRevents(uint32_t revents) { revents_ = revents; }  // 由 EpollPoller 填写
  void handleEvent();  // 根据 revents 触发对应回调（事件分发核心）
  void remove();       // 从 EventLoop/poller 中移除

 private:
  void update();  // 把当前 events 同步给 EpollPoller（ADD/MOD/DEL）

  static constexpr int kNoneEvent = 0;
  static constexpr int kReadEvent = EPOLLIN;   // 直接复用 epoll 事件值，无需映射
  static constexpr int kWriteEvent = EPOLLOUT;

  EventLoop* loop_;        // 所属事件循环（update 时回写 poller）
  int fd_;                 // 关联 fd（不拥有，不负责 close）
  uint32_t events_ = 0;    // 本 Channel 当前关心的事件
  uint32_t revents_ = 0;   // epoll_wait 回报的事件（每次 poll 覆盖）

  // 事件 -> 回调
  EventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace minirpc
