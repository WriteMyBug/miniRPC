#pragma once

#include <atomic>
#include <mutex>

#include <google/protobuf/service.h>

#include "minirpc/common/ErrorCode.h"
#include "minirpc/net/Buffer.h"
#include "minirpc/net/InetAddress.h"

namespace minirpc {

class RpcController;

// 同步 RPC 客户端通道：阻塞 IO + poll 等待，同一通道的调用串行执行。
// 每个请求带唯一序列号；超时自动重试（每次重试使用新序列号）。
class RpcChannel : public google::protobuf::RpcChannel {
 public:
  RpcChannel(const InetAddress& serverAddr, int timeoutMs = 2000,
             int maxRetries = 2);
  ~RpcChannel() override;

  void CallMethod(const google::protobuf::MethodDescriptor* method,
                  google::protobuf::RpcController* controller,
                  const google::protobuf::Message* request,
                  google::protobuf::Message* response,
                  google::protobuf::Closure* done) override;

  bool connected() const { return fd_ >= 0; }
  void close();

 private:
  enum class CallResult { kOk, kTimeout, kClosed, kSystem };

  bool connect();
  bool sendAll(const char* data, size_t len);
  CallResult waitResponse(uint64_t seq, int timeoutMs,
                          google::protobuf::Message* response,
                          google::protobuf::RpcController* controller);
  static void failController(google::protobuf::RpcController* controller,
                             ErrorCode code, const std::string& message);

  InetAddress serverAddr_;
  int fd_ = -1;
  int timeoutMs_;
  int maxRetries_;
  std::atomic<uint64_t> nextSeq_{1};
  Buffer inputBuffer_;
  std::mutex ioMutex_;  // 串行化发送与接收
};

}  // namespace minirpc
