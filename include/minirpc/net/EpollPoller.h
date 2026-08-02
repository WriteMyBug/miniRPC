#pragma once

#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class Channel;

// epoll 封装：负责 channel 增删改与事件分发。
class EpollPoller : public Noncopyable {
 public:
  EpollPoller();
  ~EpollPoller();

  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);
  void poll(int timeoutMs, std::vector<Channel*>* activeChannels);

 private:
  void fillActiveChannels(int numEvents, std::vector<Channel*>* activeChannels);
  void update(int operation, Channel* channel);

  int epollFd_;
  std::vector<epoll_event> events_;
  std::unordered_map<int, Channel*> channels_;
};

}  // namespace minirpc

