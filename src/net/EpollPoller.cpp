#include "minirpc/net/EpollPoller.h"

#include <cstring>
#include <unistd.h>

#include "minirpc/common/Logger.h"
#include "minirpc/net/Channel.h"

namespace minirpc {

namespace {
constexpr int kMaxEvents = 1024;
}  // namespace

EpollPoller::EpollPoller()
    : epollFd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kMaxEvents) {
  if (epollFd_ < 0) {
    LOG_FATAL << "epoll_create1 failed: " << std::strerror(errno);
    ::abort();
  }
}

EpollPoller::~EpollPoller() {
  ::close(epollFd_);
}

void EpollPoller::poll(int timeoutMs, std::vector<Channel*>* activeChannels) {
  const int numEvents =
      ::epoll_wait(epollFd_, events_.data(),
                   static_cast<int>(events_.size()), timeoutMs);
  if (numEvents < 0) {
    if (errno != EINTR) {
      LOG_ERROR << "epoll_wait failed: " << std::strerror(errno);
    }
    return;
  }
  fillActiveChannels(numEvents, activeChannels);
}

void EpollPoller::fillActiveChannels(
    int numEvents, std::vector<Channel*>* activeChannels) {
  for (int i = 0; i < numEvents; ++i) {
    auto* channel = static_cast<Channel*>(events_[i].data.ptr);
    channel->setRevents(events_[i].events);
    activeChannels->push_back(channel);
  }
}

void EpollPoller::updateChannel(Channel* channel) {
  const int fd = channel->fd();
  const auto it = channels_.find(fd);
  if (it == channels_.end()) {
    if (channel->isNoneEvent()) {
      return;
    }
    channels_[fd] = channel;
    update(EPOLL_CTL_ADD, channel);
  } else if (channel->isNoneEvent()) {
    update(EPOLL_CTL_DEL, channel);
    channels_.erase(fd);
  } else {
    update(EPOLL_CTL_MOD, channel);
  }
}

void EpollPoller::removeChannel(Channel* channel) {
  const int fd = channel->fd();
  const auto it = channels_.find(fd);
  if (it != channels_.end()) {
    update(EPOLL_CTL_DEL, channel);
    channels_.erase(fd);
  }
}

void EpollPoller::update(int operation, Channel* channel) {
  epoll_event event{};
  event.events = channel->events();
  event.data.ptr = channel;
  if (::epoll_ctl(epollFd_, operation, channel->fd(), &event) < 0) {
    LOG_ERROR << "epoll_ctl op=" << operation << " fd=" << channel->fd()
              << " failed: " << std::strerror(errno);
  }
}

}  // namespace minirpc

