#pragma once

namespace minirpc {

// 基类：删除拷贝构造与拷贝赋值，RAII 类继承它。
class Noncopyable {
 public:
  Noncopyable(const Noncopyable&) = delete;
  Noncopyable& operator=(const Noncopyable&) = delete;

 protected:
  Noncopyable() = default;
  ~Noncopyable() = default;
};

}  // namespace minirpc

