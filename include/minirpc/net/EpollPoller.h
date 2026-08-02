#pragma once

#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

#include "minirpc/common/Noncopyable.h"

namespace minirpc {

class Channel;

// epoll 封装：对内核 epoll 的薄封装。
// 职责：updateChannel/removeChannel 增删改（epoll_ctl），
// poll 调用 epoll_wait 并把就绪事件填进 activeChannels。
// 关键：注册时 data.ptr 直接存 Channel*，就绪时零查找取回 Channel。
class EpollPoller : public Noncopyable {
 public:
  EpollPoller();
  ~EpollPoller();

  // 新 Channel -> ADD；events 变 0 -> DEL；否则 -> MOD。
  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);
  // 阻塞最多 timeoutMs 毫秒，把就绪 Channel 追加到 activeChannels。
  void poll(int timeoutMs, std::vector<Channel*>* activeChannels);

 private:
  void fillActiveChannels(int numEvents, std::vector<Channel*>* activeChannels);
  void update(int operation, Channel* channel);

  int epollFd_;                              // epoll 文件描述符
  std::vector<epoll_event> events_;          // epoll_wait 结果缓冲（上限 kMaxEvents）
  std::unordered_map<int, Channel*> channels_;  // fd -> Channel，增删改查用
};

}  // namespace minirpc
