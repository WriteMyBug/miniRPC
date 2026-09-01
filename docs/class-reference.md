# MiniRPC 类详解与类关系

> 本文档以源码为准（include/minirpc、src），按模块逐类说明职责、关键成员、关键方法与类间关系。
> 术语约定：**loop 线程** = EventLoop 所在线程；**池线程** = ThreadPool 工作线程。

## 1. 总览

### 1.1 模块分层

```text
应用层      examples/（EchoServiceImpl / CalculatorServiceImpl，继承生成的 Service）
   | 继承 google::protobuf::Service
治理层      RegistryServiceImpl（注册中心） · LoadBalanceChannel · RpcServiceNode · RoundRobin · ConsistentHash
   |
RPC 层      RpcServer · RpcChannel · RpcController · ServiceRegistry
   |
并发层      ThreadPool（PriorityQueue + 条件变量）
   |
协议层      Codec · ProtocolHeader/ProtocolMessage（24B 头 + protobuf 信封）
   |
网络层      EventLoop · EpollPoller · Channel · Buffer · TcpServer · TcpConnection · Socket · InetAddress
   |
基础层      Noncopyable · Logger · ErrorCode
```

### 1.2 类索引

| 类 | 模块 | 一句话职责 |
|---|---|---|
| Noncopyable | common | 禁用拷贝的基类 |
| Logger / LogStream | common | 线程安全分级日志（流式） |
| ErrorCode | common | 错误码枚举 + 字符串化 |
| InetAddress | net | sockaddr_in 封装 |
| Socket | net | 非阻塞 fd 的 RAII |
| Buffer | net | 读写游标字节缓冲（粘包半包） |
| Channel | net | fd + 事件 + 回调 |
| EpollPoller | net | epoll_ctl / epoll_wait 封装 |
| EventLoop | net | 单线程事件循环（心脏） |
| TcpServer | net | 监听 + 连接管理 |
| TcpConnection | net | 单连接读写与生命周期 |
| PrioritizedTask 等 | thread | 线程池辅助类型 |
| ThreadPool | thread | 任务队列 + 工作线程 |
| ProtocolHeader / ProtocolMessage | codec | 24B 协议头与报文 |
| Codec | codec | encode / tryDecode |
| ServiceRegistry | rpc | 服务完整名 -> Service 映射 |
| RpcController | rpc | protobuf RpcController 实现 + 错误码 |
| RpcServer | rpc | 服务端组装：TcpServer + 注册表 + 线程池 |
| RpcChannel | rpc | 同步客户端通道（seq 匹配 / 超时重试） |
| LoadBalanceChannel | rpc | 发现节点 + 负载均衡 + 故障转移 |
| RoundRobin | rpc | 轮询选路 |
| ConsistentHash | rpc | 一致性哈希环 |
| RpcServiceNode | rpc | 节点自动注册 + 心跳 |
| RegistryServiceImpl | registry | 注册中心服务实现 |

## 2. 基础层（common）

### 2.1 Noncopyable

- 文件：[include/minirpc/common/Noncopyable.h](../include/minirpc/common/Noncopyable.h)
- 职责：删除拷贝构造与拷贝赋值，强制"资源类不可复制"。
- 派生类：Socket、Channel、EpollPoller、EventLoop、TcpServer、TcpConnection、ServiceRegistry、RpcServer、RpcServiceNode。
- 说明：ThreadPool 未继承它，而是手动 delete 拷贝（行为等价）；Buffer 允许拷贝（vector<char> 值语义）。

### 2.2 Logger

- 文件：[include/minirpc/common/Logger.h](../include/minirpc/common/Logger.h)
- 单例：`Logger::instance()`。
- 关键成员：`LogLevel level_`（默认 kInfo）。
- 关键方法：`setLogLevel / logLevel`、`log(level, msg)`（级别过滤 + 时间戳/线程 id 输出到 stderr，内部 static mutex，线程安全）。
- 嵌套类 `LogStream`：RAII 流式日志，析构时调用 `logger_.log`；宏 `LOG_INFO << x` 展开为临时 LogStream。

### 2.3 ErrorCode

- 文件：[include/minirpc/common/ErrorCode.h](../include/minirpc/common/ErrorCode.h)
- `enum class ErrorCode`：kOk=0 / kSystem=1 / kInvalidArgument=2 / kTimeout=3 / kClosed=4 / kNoService=5 / kNoMethod=6 / kUnknown=99。
- 自由函数 `errorCodeToString`：错误码 -> 字符串。

## 3. 网络层（net）

### 3.1 InetAddress

- 文件：[include/minirpc/net/InetAddress.h](../include/minirpc/net/InetAddress.h)
- 职责：IPv4 地址封装。
- 关键成员：`sockaddr_in addr_`。
- 关键方法：构造（port / ip+port / sockaddr_in）、`toIp / toIpPort / port`、`sockAddr()`。
- 相关类：被 Socket、TcpServer、TcpConnection、RpcChannel、RpcServiceNode 使用。

### 3.2 Socket

- 文件：[include/minirpc/net/Socket.h](../include/minirpc/net/Socket.h)
- 职责：非阻塞 fd 的 RAII 封装（析构 close）。
- 关键成员：`int fd_`。
- 关键方法：`bindAddress`（失败 FATAL）、`listen`、`accept`（accept4，新 fd 自带非阻塞 + CLOEXEC）、`shutdownWrite`（SHUT_WR 半关闭）、`setReuseAddr/ReusePort/KeepAlive/TcpNoDelay`。
- 相关类：TcpServer 持有监听 Socket；TcpConnection 按值持有连接 Socket。

### 3.3 Buffer

- 文件：[include/minirpc/net/Buffer.h](../include/minirpc/net/Buffer.h)
- 职责：TCP 粘包/半包字节缓冲。
- 关键成员：`vector<char> buffer_`、`readerIndex_`、`writerIndex_`；常量 `kCheapPrepend=8`、`kInitialSize=1024`。
- 关键方法：`readableBytes/writableBytes/prependableBytes`、`peek`（只看不取）、`retrieve(n)`（消费）、`retrieveAsString/retrieveAllAsString`、`append`（自动扩容）、`readFd`（readv 双缓冲一次读尽）。
- 所有权：TcpConnection 的 input/output Buffer、RpcChannel 的 inputBuffer_ 各自持有；Buffer 只被所属线程操作。

### 3.4 Channel

- 文件：[include/minirpc/net/Channel.h](../include/minirpc/net/Channel.h)
- 职责：把 fd 与"感兴趣事件 + 回调"绑定；不拥有 fd。
- 关键成员：`loop_`、`fd_`、`events_`、`revents_`、四个回调（read/write/close/error）。
- 关键方法：`enableReading/enableWriting/disableWriting/disableAll`（改 events 并 update() 同步到 epoll）、`handleEvent`（按 revents 分发：HUP 且无 IN -> close；ERR -> error；IN/PRI/RDHUP -> read；OUT -> write）、`setRevents`（EpollPoller 填写）、`remove`。
- 生命周期：由持有者管理（TcpServer 的 acceptChannel_、TcpConnection 的 channel_ 均为 unique_ptr）。

### 3.5 EpollPoller

- 文件：[include/minirpc/net/EpollPoller.h](../include/minirpc/net/EpollPoller.h)
- 职责：epoll 封装。
- 关键成员：`epollFd_`、`events_`（结果缓冲，kMaxEvents=1024）、`unordered_map<int, Channel*> channels_`。
- 关键方法：`updateChannel`（新 fd -> ADD；事件清空 -> DEL + erase；否则 MOD）、`removeChannel`、`poll(timeoutMs, activeChannels)`（epoll_wait -> fillActiveChannels 用 data.ptr 还原 Channel*）。
- 线程归属：仅 loop 线程。

### 3.6 EventLoop

- 文件：[include/minirpc/net/EventLoop.h](../include/minirpc/net/EventLoop.h)
- 职责：单线程事件循环（网络层心脏）。
- 关键成员：`threadId_`、`poller_`(unique_ptr)、`wakeupFd_`(eventfd)、`wakeupChannel_`、`pendingFunctors_` + `mutex_`、`looping_/quit_`(atomic)。
- 关键方法：`loop`（poll(10s) -> 分发 -> doPendingFunctors）、`runInLoop`（本线程直接执行，跨线程入队）、`queueInLoop` + `wakeup`（eventfd 写 1）、`updateChannel/removeChannel`（委托 poller）、`assertInLoopThread`（线程归属断言）。
- 相关类：所有 net/rpc 服务端对象都挂在某个 EventLoop 上。

### 3.7 TcpServer

- 文件：[include/minirpc/net/TcpServer.h](../include/minirpc/net/TcpServer.h)
- 职责：监听 accept + 连接管理 + 消息分发回调。
- 关键成员：`loop_`、`listenSocket_`、`acceptChannel_`、`connections_`(map<string, shared_ptr<TcpConnection>>)、`messageCallback_`、`connectionCallback_`、`nextConnId_`。
- 关键方法：`start`（忽略 SIGPIPE、bind、listen、注册读事件）、`handleNewConnection`（accept 循环，创建 TcpConnection 入表）、`removeConnection/removeConnectionInLoop`（关闭时从表移除）。
- 与业务解耦：只暴露 setMessageCallback / setConnectionCallback；RpcServer 通过 messageCallback 接入。

### 3.8 TcpConnection

- 文件：[include/minirpc/net/TcpConnection.h](../include/minirpc/net/TcpConnection.h)
- 职责：一条已建立连接的读写、缓冲与生命周期。
- 继承：Noncopyable + `enable_shared_from_this<TcpConnection>`。
- 关键成员：`loop_`、`name_`、`state_`（Connecting->Connected->Disconnecting->Disconnected）、`socket_`、`channel_`(unique_ptr)、`localAddr_/peerAddr_`、`inputBuffer_/outputBuffer_`、回调（message/close/writeComplete）。
- 关键方法：`connectEstablished/connectDestroyed`（置 Connected 并注册读 / 清理）、`send`（线程安全：weak_ptr + runInLoop）、`sendInLoop`（快速路径直写，剩余进 outputBuffer_ + enableWriting）、`shutdown/shutdownInLoop`、`handleRead/Write/Close/Error`。
- 生命周期：shared_ptr 由 TcpServer 的 map 持有；池线程任务捕获 TcpConnectionPtr 可延长生存期；最后一个引用释放时析构并 close fd。

## 4. 并发层（thread）

### 4.1 辅助类型

- `PoolMode`：kFixed（固定线程数）/ kCached（动态扩缩）。
- `TaskPriority`：kHigh=0 / kNormal=1 / kLow=2（值越小越优先）。
- `PrioritizedTask`：`{int priority; mutable std::function<void()> func;}`，`operator<` 反转实现小顶堆（priority_queue 顶部为最高优先级）。
- `ThreadPoolConfig`：mode / initThreadCount / maxThreadCount / maxQueueSize，一次性传入 Start，避免 setter 在运行后被静默忽略。

### 4.2 ThreadPool

- 文件：[include/minirpc/thread/ThreadPool.h](../include/minirpc/thread/ThreadPool.h)
- 职责：生产者-消费者线程池，支持 future 与回调两种取结果方式。
- 关键成员：`taskQueue_`(priority_queue) + `taskQueueMutex_` + `notEmpty_/notFull_`、`threads_`(unordered_set<thread::id>) + `threadSetMutex_` + `exitCond_`、`callbackQueue_` + `callbackQueueMutex_`、原子计数（currentThreadCount_/idleThreadCount_/taskQueueSize_）、`isRunning_`。
- 关键方法：
  - `submit / submitWithPriority`（模板）：packaged_task + std::apply，返回 future；队列满超时抛 runtime_error；
  - `trySubmitFor`：超时返回 nullopt（不抛）；
  - `submitWithCallback / runPendingCallbacks`：结果入回调队列，主线程自取；
  - `Start(config)` / `Start(size_t)`；`SetMode/SetTaskQueueMaxSize/SetMaxThreadCount/SetLogEnabled`；
  - `enqueueTask`（满队列 wait_for 60s）、`WorkerThread`（等待->取堆顶->try/catch 执行）、`CreateThread`（先注册 id 再 detach）、`threadExit`、`waitForWork`。
- 线程归属：工作线程执行任务；回调队列由"主线程"调 runPendingCallbacks 消费。

## 5. 协议层（codec）

### 5.1 ProtocolHeader / ProtocolMessage / MessageType

- 文件：[include/minirpc/codec/Protocol.h](../include/minirpc/codec/Protocol.h)
- `ProtocolHeader`：24 字节（magic=4 / version=1 / type=1 / flags=2 / seq=8 / bodyLen=4 / reserved=4），`static_assert(sizeof==24)`。
- `ProtocolMessage`：`{ProtocolHeader header; std::string body;}`。
- `MessageType`：kRequest=1 / kResponse=2。
- 常量：`kMagic=0x4D494E52`、`kProtocolVersion=1`、`kHeaderSize=24`、`kMaxBodySize=64MB`。

### 5.2 Codec

- 文件：[include/minirpc/codec/Codec.h](../include/minirpc/codec/Codec.h)
- 职责：协议头 + body 的编解码（静态方法，无状态）。
- 关键方法：
  - `encode(const ProtocolMessage&, Buffer*)`：按布局顺序写大端字段 + body；
  - `tryDecode(Buffer*)`：头未齐 / body 未齐返回 nullopt 且不消费；魔数/版本/长度非法抛 runtime_error；齐了才 retrieve。
- 相关类：RpcServer::onMessage 与 RpcChannel::waitResponse 分别调用；内部匿名辅助函数 appendU16/U32/U64、readU16/U32/U64。

## 6. RPC 层（rpc）

### 6.1 ServiceRegistry

- 文件：[include/minirpc/rpc/ServiceRegistry.h](../include/minirpc/rpc/ServiceRegistry.h)
- 职责：服务完整名（package.Service）到 `google::protobuf::Service*` 的注册表。
- 关键成员：`unordered_map<string, Service*> services_` + `mutable mutex`。
- 关键方法：`addService`（重复注册返回 false）、`findService`（找不到返回 nullptr）。

### 6.2 RpcController

- 文件：[include/minirpc/rpc/RpcController.h](../include/minirpc/rpc/RpcController.h)
- 继承：`google::protobuf::RpcController`。
- 关键成员：`failed_/canceled_`(atomic)、`errorCode_`、`errorText_`、`cancelCallback_`。
- 关键方法：`Reset/Failed/ErrorText/StartCancel/SetFailed(string)/IsCanceled/NotifyOnCancel`（protobuf 接口）+ `SetFailed(ErrorCode, msg)` / `errorCode()`（扩展）。

### 6.3 RpcServer

- 文件：[include/minirpc/rpc/RpcServer.h](../include/minirpc/rpc/RpcServer.h)
- 职责：RPC 服务端组装（TcpServer + ServiceRegistry + ThreadPool）。
- 关键成员：`loop_`、`server_`(TcpServer 值成员)、`registry_`(ServiceRegistry 值成员)、`pool_`(unique_ptr<ThreadPool>)、`poolThreads_`。
- 关键方法：`registerService`（查重后入注册表）、`setThreadPoolSize`、`start`（启线程池 + TcpServer）、`onMessage`（loop 线程 while(tryDecode) + submit）、`handleRequest`（池线程：解析信封 -> 查服务/方法 -> 反射 New/Parse -> CallMethod -> 回包）、`sendError/sendEnvelope`。

### 6.4 RpcChannel

- 文件：[include/minirpc/rpc/RpcChannel.h](../include/minirpc/rpc/RpcChannel.h)
- 继承：`google::protobuf::RpcChannel`。
- 职责：同步客户端通道（阻塞 socket + poll + seq 匹配 + 超时重试）。
- 关键成员：`serverAddr_`、`fd_`、`timeoutMs_`、`maxRetries_`、`nextSeq_`(atomic)、`inputBuffer_`、`ioMutex_`。
- 关键方法：`CallMethod`（ioMutex_ 串行；每次尝试新 seq；仅 kTimeout 重试）、`waitResponse`（poll 剩余时间 -> readFd -> tryDecode -> 按 seq 匹配）、`sendAll/connect/close`、`failController`。

### 6.5 LoadBalanceChannel

- 文件：[include/minirpc/rpc/LoadBalanceChannel.h](../include/minirpc/rpc/LoadBalanceChannel.h)
- 继承：`google::protobuf::RpcChannel`。
- 职责：从注册中心发现节点，按策略选节点，故障转移。
- 关键成员：`registryAddr_/serviceName_/type_`、`refreshIntervalMs_/callTimeoutMs_/callRetries_`、`nodes_`(vector<NodeInfo>)、`channels_`(map<string, unique_ptr<RpcChannel>>)、`registryChannel_ + registryStub_`、`roundRobin_`、`consistentHash_`、`failedNodeKey_`。
- 关键方法：`CallMethod`（needRefresh -> refreshNodes -> selectNodeKey -> channelFor 调用；连接级错误刷新后换节点重试一次）、`needRefresh/refreshNodes/selectNodeKey/selectNodeKeyForRetry/channelFor/nodeKeyOf`、`nodeCount`。

### 6.6 RoundRobin

- 文件：[include/minirpc/rpc/LoadBalancer.h](../include/minirpc/rpc/LoadBalancer.h)
- 关键成员：`atomic<size_t> index_`、`nodeCount_`。
- 关键方法：`setNodeCount`、`next()`（`index_++ % nodeCount_`）。

### 6.7 ConsistentHash

- 文件：[include/minirpc/rpc/LoadBalancer.h](../include/minirpc/rpc/LoadBalancer.h)
- 职责：虚拟节点 + 有序环一致性哈希。
- 关键成员：`ring_`(map<uint64_t, string>)、`virtualNodesPerNode_`（默认 100）、`nodeKeys_`。
- 关键方法：`setNodes`（每节点插入 "key#i" 虚拟点）、`selectNode`（lower_bound 顺时针）、`hash64`（std::hash<string>，弃用 FNV-1a 原因见 [docs/decisions.md](decisions.md) D6）。

### 6.8 RpcServiceNode

- 文件：[include/minirpc/rpc/RpcServiceNode.h](../include/minirpc/rpc/RpcServiceNode.h)
- 职责：服务节点自动注册 + 心跳。
- 关键成员：`server_`(RpcServer 值成员)、`registryAddr_/serviceName_`、`heartbeatIntervalMs_/heartbeatTimeoutMs_`、`heartbeatThread_`、`stop_`(atomic)、`cv_`。
- 关键方法：`start`（server.start + registerSelf + 心跳线程）、`stopHeartbeat`（停心跳不注销，模拟崩溃）、`shutdown`（停心跳 + unregisterSelf，优雅下线）、`registerSelf/unregisterSelf/heartbeatLoop`。

## 7. 注册中心（registry）

### 7.1 RegistryServiceImpl

- 文件：[include/minirpc/registry/RegistryServiceImpl.h](../include/minirpc/registry/RegistryServiceImpl.h)
- 继承：`minirpc::registry::RegistryService`（protoc 生成，间接继承 google::protobuf::Service）。
- 职责：注册/发现/心跳剔除。
- 关键成员：`services_`(unordered_map<serviceName, unordered_map<nodeKey, NodeEntry>>)、`heartbeatTimeout_`、`stopEviction_`(atomic)、`evictionThread_`；`NodeEntry = {NodeInfo node; time_point lastHeartbeat;}`。
- 关键方法：`Register`（写入 + now）、`Unregister`（删除）、`Discover`（purge 后返回排序节点）、`Heartbeat`（刷新时间戳，未注册报错）、`liveNodeCount`、`purgeExpired`（超时删除 + 日志）、`evictionLoop`（每 500ms 扫描）、`nodeKey`（host:port）。

## 8. 类之间的关系

### 8.1 继承关系

```text
Noncopyable
  ├─ Socket, Channel, EpollPoller, EventLoop
  ├─ TcpServer, TcpConnection（另继承 enable_shared_from_this<TcpConnection>）
  ├─ ServiceRegistry, RpcServer, RpcServiceNode

google::protobuf::RpcChannel
  ├─ RpcChannel
  └─ LoadBalanceChannel

google::protobuf::RpcController
  └─ RpcController

google::protobuf::Service
  ├─ 生成的 EchoService / CalculatorService / RegistryService
  │    ├─ RegistryServiceImpl
  │    └─ 业务实现类（examples 中的 EchoServiceImpl / CalculatorServiceImpl）
```

### 8.2 组合与持有关系

```text
EventLoop ──unique_ptr──> EpollPoller, wakeupChannel
                          （vector<Functor> pendingFunctors_ + mutex）

TcpServer ──值──> Socket listenSocket_
           ──unique_ptr──> Channel acceptChannel_
           ──map──> shared_ptr<TcpConnection> connections_

TcpConnection ──值──> Socket socket_, InetAddress x2, Buffer x2
               ──unique_ptr──> Channel channel_

RpcServer ──值──> TcpServer server_, ServiceRegistry registry_
           ──unique_ptr──> ThreadPool pool_

RpcServiceNode ──值──> RpcServer server_
               ──thread──> 心跳线程

LoadBalanceChannel ──unique_ptr──> RpcChannel registryChannel_
                    ──unique_ptr──> RegistryService::Stub registryStub_
                    ──map──> unique_ptr<RpcChannel> channels_（每节点一条）
                    ──值──> RoundRobin, ConsistentHash

ThreadPool ──值──> priority_queue<PrioritizedTask>, callbackQueue_,
                   unordered_set<thread::id> threads_
```

### 8.3 回调绑定（运行时关系）

```text
acceptChannel_.readCallback_  ──> TcpServer::handleNewConnection
conn.channel_.readCallback_   ──> TcpConnection::handleRead
conn.channel_.writeCallback_  ──> TcpConnection::handleWrite
conn.channel_.closeCallback_  ──> TcpConnection::handleClose
conn.messageCallback_         ──> RpcServer::onMessage（TcpServer 注入）
conn.closeCallback_           ──> TcpServer::removeConnection
RpcServer::onMessage          ──submit──> pool_ ──> RpcServer::handleRequest
handleRequest                 ──CallMethod──> 业务 Service 实现
客户端 Stub（生成的）          ──> RpcChannel::CallMethod / LoadBalanceChannel::CallMethod
```

### 8.4 线程与所有权模型

| 线程 | 操作的对象 | 说明 |
|---|---|---|
| loop 线程 | EventLoop、EpollPoller、Channel、TcpServer、TcpConnection 及其 input/output Buffer | 网络对象唯一合法操作线程（assertInLoopThread 强制） |
| 池线程 | 线程池任务（RpcServer::handleRequest、业务 Service 方法） | 回包经 conn->send -> runInLoop 投回 loop 线程 |
| 调用方线程 | RpcChannel / LoadBalanceChannel（ioMutex_ 串行） | 同步调用；LoadBalanceChannel 内部 mutex_ 串行化整条调用链 |
| 心跳线程 | RpcServiceNode 的心跳 RpcChannel | 独立于服务端 loop |
| 剔除线程 | RegistryServiceImpl::services_（mutex_ 保护） | 每 500ms purgeExpired |

### 8.5 关键所有权规则（面试常考）

1. Channel **不拥有 fd**：fd 归 Socket/TcpConnection 管，Channel 只持有 int 与回调；
2. Buffer **不跨线程共享**：input/output Buffer 属于 loop 线程，池线程只能拿"拷贝出来的字符串"；
3. TcpConnection 用 **shared_ptr 管理**：map + 池任务都可能持有，关闭后对象存活到最后一个引用释放；
4. ThreadPool 的工作线程 **detach**，但线程 id 先注册进 threads_ 再 detach，析构等集合清空；
5. RpcServer 与 RpcServiceNode 按**值持有** TcpServer/RpcServer：外层生命周期必须长于内层。

