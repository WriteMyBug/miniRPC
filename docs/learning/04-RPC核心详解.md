# 阶段 4：RPC 核心详解（5h）

对应代码：`include/minirpc/rpc/`、`src/rpc/`（RpcServer/RpcChannel/RpcController/ServiceRegistry）。

## 1. 学习目标

- 能画出一张图讲清"客户端 Stub 调用 → 服务端 Service 方法执行 → 回包"；
- 能讲清服务端如何用 protobuf 反射做通用分发（新增服务不用改框架）；
- 能讲清同步通道的 seq 匹配、超时重试为什么不会串包；
- 能讲清跨线程回包为什么安全。

## 2. 前置知识

- 阶段 1-3 全部；
- protobuf 反射：`Descriptor`、`MethodDescriptor`、`GetRequestPrototype()/New()`、`CallMethod`；
- `google::protobuf::Service` 与生成的 `Service::Stub` 的关系。

## 3. 架构设计详解

### 3.1 一次调用的全链路（最终必考图）

```text
客户端                                  服务端
─────────────────────────              ─────────────────────────
App ──> CalculatorService::Stub
         │
         └──> RpcChannel::CallMethod        RpcServer
                │ 组装信封+协议头             │ TcpServer 收数据
                │ 发请求(seq=N)  ────────>   │ Codec::tryDecode 拆包
                │ 阻塞等待                    │ 提交 ThreadPool
                │                            │ handleRequest:
                │                            │   解析信封 → 查注册表
                │                            │   → 反射 New 请求对象
                │                            │   → Service::CallMethod
                │                            │   → 序列化响应信封
                │ <──── 响应(seq=N) ────────── 回包
                │ 按 seq 匹配 → 填 response / 错误
```

### 3.2 关键设计：注册表 + 反射 = 通用分发

服务端不用为每个服务写分发代码，靠两条信息：

```text
method 全名 "minirpc.example.CalculatorService.Calc"
   ├─ 取最后一个点 → 服务名 "minirpc.example.CalculatorService" → 查 ServiceRegistry
   └─ 方法名 "Calc" → FindMethodByName → MethodDescriptor
然后 GetRequestPrototype(method).New() 创建请求对象 → ParseFromString → CallMethod
```

新增一个服务 = 写一个 proto + 继承生成类实现方法 + `registerService` 一行。这就是框架的"可扩展性"。

## 4. 重点实现拆解

### 4.1 ServiceRegistry（src/rpc/ServiceRegistry.cpp）

就是一个 `unordered_map<完整服务名, Service*>` + 互斥锁，`addService` 重复注册返回 false，`findService` 找不到返回 nullptr。没有魔法，30 行。

### 4.2 RpcServer 服务端路径（src/rpc/RpcServer.cpp）

```text
onMessage（loop 线程）:
  while (auto msg = Codec::tryDecode(buffer))    // 拆出完整报文
    pool_->submit([conn, msg] { handleRequest(conn, msg); })

handleRequest（线程池线程）:
  解析 RpcRequest 信封
  查服务/方法（找不到 → sendError(kNoService/kNoMethod)）
  创建请求/响应对象并反序列化
  RpcController controller
  service->CallMethod(method, &controller, req, resp, nullptr)
  controller.Failed() ? 回错误信封 : 回成功信封
```

注意两个线程边界：
- **拆包在 loop 线程**：Buffer 是 loop 线程独有的，不能给线程池共享；
- **业务在线程池线程**：`CallMethod` 可能耗时（如查数据库），不能阻塞事件循环；
- **回包走 `conn->send()`**：内部 `runInLoop` 把写操作投回 loop 线程，多个工作线程同时回包也不会抢一个 fd。

### 4.3 RpcChannel 同步调用（src/rpc/RpcChannel.cpp）

为什么是"阻塞 socket + poll"而不是独立接收线程？
- 同步调用天然串行：一个请求发出后阻塞等它，poll 超时由调用方控制；
- 用互斥锁把整个 CallMethod 串行化，多线程并发调用也不会把响应张冠李戴。

```text
CallMethod:
  lock(ioMutex_)
  controller->Reset()                      // 清上次失败标记
  for (attempt = 0; attempt <= maxRetries_; ++attempt):
    seq = nextSeq_++                        // 每次重试都用新 seq
    编码 协议头(type=request,seq) + 信封 → sendAll
    waitResponse(seq, timeoutMs):
      poll(fd, 剩余时间) → readFd 攒进 inputBuffer_
      while (msg = Codec::tryDecode)：
        msg.seq == 本请求 seq? → 解析 RpcResponse → 成功/业务错误
        否则 → 丢弃（旧重试的迟到响应）
    poll 超时 → 若还有重试机会 → 回到循环头（新 seq 重发）
             → 否则 controller->SetFailed(kTimeout)
```

为什么重试要换新 seq？因为旧请求可能其实被服务端执行了（只是响应丢了/迟到）。换新 seq 后，旧响应即使迟到，seq 对不上也不会污染新请求——这是"可重放"的代价，面试要主动讲。

### 4.4 RpcController（src/rpc/RpcController.cpp）

实现 protobuf 的 RpcController 接口（Reset/Failed/ErrorText/SetFailed/StartCancel/IsCanceled/NotifyOnCancel），扩展了带错误码的 `SetFailed(ErrorCode, msg)`。服务方法里：

```cpp
if (b == 0) controller->SetFailed("division by zero");
```

框架自动把 `Failed + ErrorText` 装进 RpcResponse 信封传回客户端。这就是"业务错误走控制器，不走异常/不走断开"。

### 4.5 服务端反射调用的完整代码（读这一小段就够）

```cpp
std::unique_ptr<Message> request(service->GetRequestPrototype(method).New());
request->ParseFromString(envelope.request());
std::unique_ptr<Message> response(service->GetResponsePrototype(method).New());
RpcController controller;
service->CallMethod(method, &controller, request.get(), response.get(), nullptr);
```

`New()` 是多态创建：运行时根据方法描述符创建对应消息类型，所以框架代码不认识 CalculatorRequest 也能处理它。

## 5. 面试追问预演（先自己答，再看 interview-qa.md）

1. 服务端怎么做到"加一个服务不用改框架"？
2. 两个客户端同时调不同方法，线程池里并发执行，回包会不会乱序/错乱？
3. 超时后服务端其实执行了，重试会不会重复执行？你的设计怎么处理？
4. 为什么同步通道要加互斥锁？不加会怎样？
5. 如果线程池队列满了，请求会怎样？（提示：submit 抛异常，服务端回 kSystem 错误）

## 6. 小练习

- 练习 A（必做，1h）：把 `calculator_client.cpp` 改成"循环调用 100 次，统计平均耗时"，用 `RpcController::errorCode()` 打印任何失败的错误码。
- 练习 B（必做，1h）：给 proto 加一个新方法 `Pow`（a 的 b 次方，b 为整数），改服务端实现、客户端调用、跑通。**这就是"新增服务不用改框架"的亲身验证。**
- 练习 C（选做）：把 maxRetries 改成 5，对黑洞服务端（`sink`）调用一次，观察总耗时约等于 5×超时。

