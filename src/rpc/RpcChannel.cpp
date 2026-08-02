#include "minirpc/rpc/RpcChannel.h"

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <string>

#include <google/protobuf/message.h>

#include "minirpc/codec/Codec.h"
#include "minirpc/codec/Protocol.h"
#include "minirpc/rpc/RpcController.h"
#include "rpc.pb.h"

namespace minirpc {

RpcChannel::RpcChannel(const InetAddress& serverAddr, int timeoutMs,
                       int maxRetries)
    : serverAddr_(serverAddr), timeoutMs_(timeoutMs), maxRetries_(maxRetries) {}

RpcChannel::~RpcChannel() {
  close();
}

void RpcChannel::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool RpcChannel::connect() {
  if (fd_ >= 0) {
    return true;
  }
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  if (::connect(fd, reinterpret_cast<const sockaddr*>(serverAddr_.sockAddr()),
                sizeof(sockaddr_in)) < 0) {
    ::close(fd);
    return false;
  }
  fd_ = fd;
  return true;
}

bool RpcChannel::sendAll(const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const ssize_t n = ::send(fd_, data + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                            google::protobuf::RpcController* controller,
                            const google::protobuf::Message* request,
                            google::protobuf::Message* response,
                            google::protobuf::Closure* done) {
  std::lock_guard<std::mutex> lock(ioMutex_);
  if (controller != nullptr) {
    controller->Reset();
  }

  internal::RpcRequest envelope;
  envelope.set_method(method->full_name());
  request->SerializeToString(envelope.mutable_request());
  std::string envelopeBytes;
  envelope.SerializeToString(&envelopeBytes);

  if (!connect()) {
    failController(controller, ErrorCode::kSystem, std::strerror(errno));
    if (done != nullptr) {
      done->Run();
    }
    return;
  }

  CallResult result = CallResult::kSystem;
  for (int attempt = 0; attempt <= maxRetries_; ++attempt) {
    if (controller != nullptr) {
      controller->Reset();  // 清除上一次尝试（超时）留下的失败标记
    }
    ProtocolMessage msg;
    msg.header.magic = kMagic;
    msg.header.version = kProtocolVersion;
    msg.header.type = static_cast<uint8_t>(MessageType::kRequest);
    msg.header.seq = nextSeq_++;
    msg.body = envelopeBytes;
    Buffer out;
    Codec::encode(msg, &out);

    if (!sendAll(out.peek(), out.readableBytes())) {
      failController(controller, ErrorCode::kSystem, std::strerror(errno));
      close();
      result = CallResult::kSystem;
      break;
    }

    result = waitResponse(msg.header.seq, timeoutMs_, response, controller);
    if (result != CallResult::kTimeout) {
      break;
    }
    // 超时：若还有重试机会，以新序列号重发
  }

  if (done != nullptr) {
    done->Run();
  }
}

RpcChannel::CallResult RpcChannel::waitResponse(
    uint64_t seq, int timeoutMs, google::protobuf::Message* response,
    google::protobuf::RpcController* controller) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      failController(controller, ErrorCode::kTimeout, "rpc call timeout");
      return CallResult::kTimeout;
    }
    const int remainingMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            .count());
    pollfd pfd{fd_, POLLIN, 0};
    const int rc = ::poll(&pfd, 1, remainingMs);
    if (rc == 0) {
      failController(controller, ErrorCode::kTimeout, "rpc call timeout");
      return CallResult::kTimeout;
    }
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      failController(controller, ErrorCode::kSystem, std::strerror(errno));
      return CallResult::kSystem;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      failController(controller, ErrorCode::kClosed, "connection closed");
      return CallResult::kClosed;
    }
    if (!(pfd.revents & POLLIN)) {
      continue;
    }
    int savedErrno = 0;
    const ssize_t n = inputBuffer_.readFd(fd_, &savedErrno);
    if (n <= 0) {
      failController(controller, ErrorCode::kClosed, "connection closed");
      return CallResult::kClosed;
    }
    try {
      while (auto msgOpt = Codec::tryDecode(&inputBuffer_)) {
        const ProtocolMessage& m = *msgOpt;
        if (m.header.type == static_cast<uint8_t>(MessageType::kResponse) &&
            m.header.seq == seq) {
          internal::RpcResponse resp;
          if (!resp.ParseFromString(m.body)) {
            failController(controller, ErrorCode::kUnknown,
                           "bad response envelope");
            return CallResult::kSystem;
          }
          const int32_t code = resp.error_code();
          if (code != static_cast<int32_t>(ErrorCode::kOk)) {
            failController(controller, static_cast<ErrorCode>(code),
                           resp.error_message().empty()
                               ? "rpc call failed"
                               : resp.error_message());
            return CallResult::kSystem;  // 服务端已明确报错，不重试
          }
          if (!response->ParseFromString(resp.response())) {
            failController(controller, ErrorCode::kUnknown,
                           "bad response payload");
            return CallResult::kSystem;
          }
          return CallResult::kOk;
        }
        // 其它序列号的响应（重试残留）→ 丢弃
      }
    } catch (const std::exception& e) {
      failController(controller, ErrorCode::kUnknown, e.what());
      return CallResult::kSystem;
    }
  }
}

void RpcChannel::failController(google::protobuf::RpcController* controller,
                                ErrorCode code, const std::string& message) {
  if (controller == nullptr) {
    return;
  }
  if (auto* rc = dynamic_cast<RpcController*>(controller)) {
    rc->SetFailed(code, message);
  } else {
    controller->SetFailed(message);
  }
}

}  // namespace minirpc
