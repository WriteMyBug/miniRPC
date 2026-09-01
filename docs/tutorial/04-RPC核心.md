# 04 · RPC 核心（4h）

对应代码：include/minirpc/rpc/、src/rpc/（RpcServer/RpcChannel/RpcController/ServiceRegistry）。

## 1. 学习目标

1. 能画出并讲清一次同步 RPC 调用从 Stub 到 Service 再到回包的完整时序；
2. 能讲清 protobuf 反射分发（GetRequestPrototype/New/CallMethod）如何实现"新增服务不改框架"；
3. 能讲清 RpcChannel 的 seq 匹配、poll 超时、重试换新 seq 与每次 Reset controller 的语义；
4. 能区分"超时重试"与"业务错误不重试"的判定依据；
5. 能讲清跨线程回包为什么安全（runInLoop + weak_ptr）。

## 2. 前置知识（10 分钟自查）

| 概念 | 自查方式 |
|---|---|
| protobuf 反射 | 说出 `GetDescriptor()->FindMethodByName` 与 `GetRequestPrototype(method)->New()` 的用途 |
| Service/Stub | 说出 `cc_generic_services=true` 生成的 Service 与 Stub 的关系 |
| 1-3 章 | 复述：tryDecode 拆包、loop 线程边界、runInLoop 回包 |

## 3. 源码阅读清单

| 知识点 | 文件:函数 | 阅读要点 |
|---|---|---|
| 注册表 | [src/rpc/ServiceRegistry.cpp:addService] | map + 锁；重复注册拒绝 |
| 服务端收包 | [src/rpc/RpcServer.cpp:onMessage] | while(tryDecode)；submit 到线程池 |
| 服务端分发 | [src/rpc/RpcServer.cpp:handleRequest] | 切 method 全名→查服务/方法→反射调用 |
| 回包 | [src/rpc/RpcServer.cpp:sendEnvelope] | 同 seq 的 RpcResponse 信封 |
| 客户端调用 | [src/rpc/RpcChannel.cpp:CallMethod] | ioMutex_ 串行；每次尝试新 seq |
| 客户端等待 | [src/rpc/RpcChannel.cpp:waitResponse] | poll 剩余时间→readFd→按 seq 匹配 |
| 控制器 | [src/rpc/RpcController.cpp:Reset] / [src/rpc/RpcController.cpp:SetFailed] | 错误码扩展；Reset 清除残留状态 |

## 4. 技术原理

### 4.1 一次调用的完整时序

```text
客户端                                              服务端
─────────────────                                  ─────────────────
App --> CalculatorService::Stub
         │ RpcChannel::CallMethod
         │   1. 组装 RpcRequest{method, request} 序列化
         │   2. 24B 头(type=request, seq=N) + body 编码
         │   3. sendAll ──TCP──>  epoll 可读
         │                              │ TcpConnection::handleRead
         │                              │   inputBuffer_.readFd
         │                              │ RpcServer::onMessage
         │                              │   while(tryDecode) → submit(线程池)
         │                              │ RpcServer::handleRequest（池线程）
         │                              │   解析信封 → 查注册表
         │                              │   → GetRequestPrototype(method).New()
         │                              │   → ParseFromString
         │                              │   → Service::CallMethod（业务执行）
         │                              │   → 序列化 RpcResponse
         │                              │   → sendEnvelope（同 seq）
         │ <──TCP── 响应(seq=N) ────────│
         │ waitResponse: poll → readFd → tryDecode
         │   按 seq 匹配 → 解析 RpcResponse → resp.result()=42
         │   controller.Failed()? 错误码/错误文本
```

### 4.2 信封协议

body 是 protobuf 信封（[proto/rpc.proto]）：

```proto
message RpcRequest  { string method = 1; bytes request = 2; }
message RpcResponse { int32 error_code = 1; string error_message = 2;
                      bytes response = 3; }
```

错误码与成功数据统一走同一信封：客户端先看 `error_code`，非 0 则控制器置失败，不再解析 `response`。

### 4.3 注册表 + 反射 = 通用分发

```text
method 全名 "minirpc.example.CalculatorService.Calc"
  ├─ 最后一个 '.' 切分
  │    serviceName = "minirpc.example.CalculatorService" → ServiceRegistry
  │    methodName  = "Calc" → FindMethodByName
  └─ GetRequestPrototype(method).New()   // 多态创建，运行时才知道具体类型
     ParseFromString(envelope.request())
     CallMethod(method, &controller, req, resp, nullptr)  // done=nullptr 同步
```

新增服务 = 写 proto + 继承生成类实现方法 + `registerService` 一行，框架代码零改动。

### 4.4 线程边界与回包安全

- 拆包：loop 线程（Buffer 属 loop）；
- 业务：线程池线程（handleRequest）；
- 回包：`conn->send` 内部 `runInLoop` + `weak_ptr`，多个池线程并发回包也不会并发写同一 fd。

## 5. 重点实现拆解

### 5.1 ServiceRegistry [src/rpc/ServiceRegistry.cpp]

`unordered_map<服务完整名, Service*>` + mutex。`addService` 重复注册返回 false（RpcServer 抛 `duplicate service`），`findService` 找不到返回 nullptr（服务端回 `kNoService`）。

### 5.2 RpcServer 分发链 [src/rpc/RpcServer.cpp:onMessage] → [src/rpc/RpcServer.cpp:handleRequest]

```text
onMessage（loop 线程）:
  while (auto msg = Codec::tryDecode(buffer)):
    type != kRequest → 日志并跳过
    pool_->submit([this, conn, msg] { handleRequest(conn, msg); })
    // conn 以 shared_ptr 捕获，任务执行期间连接不析构

handleRequest（池线程）:
  ParseFromString 信封
  method 全名切分 → 查服务/方法（失败 → sendError(kNoService/kNoMethod)）
  New 请求对象 → ParseFromString
  RpcController controller
  service->CallMethod(method, &controller, req, resp, nullptr)
  controller.Failed() ? 错误信封 : 成功信封
  sendEnvelope(conn, 原 seq, resp)
```

### 5.3 RpcChannel::CallMethod [src/rpc/RpcChannel.cpp:CallMethod]

```text
lock(ioMutex_)                              // 同一通道调用串行化
controller->Reset()                         // 清残留失败状态
组装信封 → connect()（阻塞 connect，失败 kSystem）
for attempt in 0..maxRetries_:
  controller->Reset()                       // 每次尝试前重置
  seq = nextSeq_++                          // 新 seq
  编码(type=request, seq) → sendAll
  result = waitResponse(seq, timeoutMs, ...)
  result != kTimeout → break                // 成功/业务错误/连接错误都不重试
  // kTimeout → 进入下一轮尝试（新 seq 重发）
done ? done->Run() : (void)0
```

重试语义：

- 只有 `kTimeout` 触发重试；
- 连接关闭（kClosed）、系统错误（kSystem）、服务端明确报错（业务错误）都不重试；
- 每次重试使用新 seq：旧响应即使迟到也因 seq 不匹配被丢弃，不会串到新请求。

### 5.4 waitResponse [src/rpc/RpcChannel.cpp:waitResponse]

```text
deadline = now + timeoutMs
loop:
  remaining = deadline - now；remaining <= 0 → kTimeout
  poll(fd, POLLIN, remaining)
    0 → kTimeout；<0 → EINTR 重试，否则 kSystem
    POLLERR|HUP|NVAL → kClosed
  readFd 进 inputBuffer_
  while (tryDecode):
    type==kResponse && seq 匹配:
      解析 RpcResponse
      error_code != 0 → failController(错误码, 文本) → kSystem（不重试）
      否则 response->ParseFromString → kOk
    其他 seq → 丢弃（重试残留）
```

### 5.5 RpcController [src/rpc/RpcController.cpp]

实现 protobuf `RpcController` 接口（Reset/Failed/ErrorText/StartCancel/SetFailed/IsCanceled/NotifyOnCancel），扩展 `SetFailed(ErrorCode, msg)` 与 `errorCode()`。服务方法里 `controller->SetFailed(...)` 后，RpcServer 自动把错误码+文本装入响应信封。

## 6. 验证实验

### 实验 1：rpc_test

```bash
./build/tests/rpc_test
```

覆盖：[tests/rpc_test.cpp] 五个场景——正常 6×7=42、除零业务错误、echo 回环、黑洞服务端超时重试（300ms×2）、连接拒绝。

### 实验 2：calculator 全链路

```bash
./build/examples/calculator_server 8888 &
./build/examples/calculator_client 127.0.0.1 8888
```

预期输出含四则运算结果与 `ERROR(INVALID_ARGUMENT)` 两类业务错误。

### 实验 3：新增方法验证"不改框架"

在 [proto/calculator.proto] 增加 `rpc Pow(CalcRequest) returns (CalcResponse)`，在 [examples/rpc/calculator_server.cpp] 的 `CalculatorServiceImpl` 实现，客户端调用 `2^10==1024`。重编译自动重新生成 pb 代码，验证框架零改动。

### 实验 4：破坏性——改 maxRetries

把 RpcChannel 的 `maxRetries_` 改为 5，对黑洞服务端调用一次，观察总耗时约等于 5×超时；验证重试循环执行次数。

## 7. 面试追问

1. 服务端怎么做到"加服务不改框架"？（答：ServiceRegistry 完整名注册 + protobuf 反射，方法描述符驱动）
2. 两个客户端并发调用，服务端如何不乱？（答：每连接独立 Buffer、独立 seq 空间；服务端按请求报文处理，响应带原 seq）
3. 超时后服务端可能已执行，重试是否重复执行？（答：是，重试假设方法可重放/幂等；seq 去重只是防串包，不防重复执行——面试要主动指出）
4. 为什么同步通道要串行化？（答：单 socket 收发，串行保证响应与请求一一对应；并发调用交给异步客户端）
5. 业务错误为什么不重试？（答：重试只会重复拿同一错误，徒增服务端负载）

