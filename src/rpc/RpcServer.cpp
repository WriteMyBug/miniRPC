#include "minirpc/rpc/RpcServer.h"

#include <memory>
#include <stdexcept>
#include <utility>

#include "minirpc/codec/Codec.h"
#include "minirpc/common/Logger.h"
#include "minirpc/net/EventLoop.h"
#include "minirpc/rpc/RpcController.h"
#include "minirpc/thread/ThreadPool.h"
#include "rpc.pb.h"

namespace minirpc {

RpcServer::RpcServer(EventLoop* loop, const InetAddress& listenAddr,
                     std::string name)
    : loop_(loop),
      server_(loop, listenAddr, std::move(name)),
      pool_(std::make_unique<ThreadPool>()) {
  server_.setMessageCallback(
      [this](const TcpConnectionPtr& conn, Buffer* buffer) {
        onMessage(conn, buffer);
      });
}

RpcServer::~RpcServer() = default;

void RpcServer::registerService(google::protobuf::Service* service) {
  if (service == nullptr) {
    throw std::invalid_argument("RpcServer: null service");
  }
  const std::string name = service->GetDescriptor()->full_name();
  if (!registry_.addService(name, service)) {
    throw std::runtime_error("RpcServer: duplicate service: " + name);
  }
  LOG_INFO << "RpcServer: registered service " << name;
}

void RpcServer::setThreadPoolSize(size_t n) {
  poolThreads_ = n;
}

void RpcServer::start() {
  pool_->SetLogEnabled(false);
  ThreadPoolConfig config;
  config.mode = PoolMode::kFixed;
  config.initThreadCount = poolThreads_;
  config.maxThreadCount = poolThreads_;
  config.maxQueueSize = 4096;
  pool_->Start(config);
  server_.start();
  LOG_INFO << "RpcServer started with " << poolThreads_ << " worker threads";
}

void RpcServer::onMessage(const TcpConnectionPtr& conn, Buffer* buffer) {
  try {
    while (auto msgOpt = Codec::tryDecode(buffer)) {
      ProtocolMessage msg = std::move(*msgOpt);
      if (msg.header.type != static_cast<uint8_t>(MessageType::kRequest)) {
        LOG_WARN << "RpcServer: unexpected message type " << msg.header.type;
        continue;
      }
      try {
        pool_->submit([this, conn, msg = std::move(msg)]() mutable {
          handleRequest(conn, std::move(msg));
        });
      } catch (const std::exception& e) {
        LOG_ERROR << "RpcServer: submit failed: " << e.what();
        sendError(conn, msg.header.seq, ErrorCode::kSystem, e.what());
      }
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "RpcServer: codec error: " << e.what();
    conn->shutdown();
  }
}

void RpcServer::handleRequest(const TcpConnectionPtr& conn,
                              ProtocolMessage requestMsg) {
  internal::RpcRequest envelope;
  if (!envelope.ParseFromString(requestMsg.body)) {
    sendError(conn, requestMsg.header.seq, ErrorCode::kInvalidArgument,
              "bad RpcRequest envelope");
    return;
  }
  const std::string& methodFullName = envelope.method();
  const size_t dot = methodFullName.find_last_of('.');
  if (dot == std::string::npos || dot == 0 ||
      dot + 1 >= methodFullName.size()) {
    sendError(conn, requestMsg.header.seq, ErrorCode::kInvalidArgument,
              "invalid method name: " + methodFullName);
    return;
  }
  const std::string serviceName = methodFullName.substr(0, dot);
  const std::string methodName = methodFullName.substr(dot + 1);

  google::protobuf::Service* service = registry_.findService(serviceName);
  if (service == nullptr) {
    sendError(conn, requestMsg.header.seq, ErrorCode::kNoService,
              "no such service: " + serviceName);
    return;
  }
  const google::protobuf::MethodDescriptor* method =
      service->GetDescriptor()->FindMethodByName(methodName);
  if (method == nullptr) {
    sendError(conn, requestMsg.header.seq, ErrorCode::kNoMethod,
              "no such method: " + methodName);
    return;
  }

  std::unique_ptr<google::protobuf::Message> request(
      service->GetRequestPrototype(method).New());
  if (!request->ParseFromString(envelope.request())) {
    sendError(conn, requestMsg.header.seq, ErrorCode::kInvalidArgument,
              "bad request payload");
    return;
  }
  std::unique_ptr<google::protobuf::Message> response(
      service->GetResponsePrototype(method).New());

  RpcController controller;
  service->CallMethod(method, &controller, request.get(), response.get(),
                      nullptr);

  internal::RpcResponse resp;
  if (controller.Failed()) {
    resp.set_error_code(static_cast<int32_t>(controller.errorCode()));
    resp.set_error_message(controller.ErrorText());
  } else {
    resp.set_error_code(static_cast<int32_t>(ErrorCode::kOk));
    response->SerializeToString(resp.mutable_response());
  }
  sendEnvelope(conn, requestMsg.header.seq, resp);
}

void RpcServer::sendError(const TcpConnectionPtr& conn, uint64_t seq,
                          ErrorCode code, const std::string& message) {
  internal::RpcResponse resp;
  resp.set_error_code(static_cast<int32_t>(code));
  resp.set_error_message(message);
  sendEnvelope(conn, seq, resp);
}

void RpcServer::sendEnvelope(const TcpConnectionPtr& conn, uint64_t seq,
                             const google::protobuf::Message& envelope) {
  std::string body;
  envelope.SerializeToString(&body);
  ProtocolMessage msg;
  msg.header.magic = kMagic;
  msg.header.version = kProtocolVersion;
  msg.header.type = static_cast<uint8_t>(MessageType::kResponse);
  msg.header.seq = seq;
  msg.body = std::move(body);
  Buffer out;
  Codec::encode(msg, &out);
  conn->send(out.retrieveAllAsString());
}

}  // namespace minirpc
