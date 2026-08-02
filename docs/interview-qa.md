# MiniRPC 面试 Q&A

按模块整理，覆盖实现细节与"为什么这么设计"。

## 1. 网络层（EventLoop / EpollPoller / Buffer / TcpServer）

**Q1：为什么用 epoll 而不是多线程每连接一模型？**
高并发下每连接一个线程的代价是线程栈内存（约 8MB 虚拟内存）与上下文切换；epoll 用事件驱动，一个线程管理成千上万个 fd。我们采用"主 Reactor 单线程监听 + 线程池处理业务"：IO 事件集中在一个事件循环，避免锁竞争；耗时业务下沉线程池，避免阻塞事件循环。

**Q2：epoll 的 LT 和 ET 有什么区别？你选哪个？**
LT 是水平触发：只要缓冲区还有数据，每次 epoll_wait 都会返回；ET 是边沿触发：只在状态变化时通知一次，需要读到 EAGAIN。我们先用 LT，简单可靠、不容易丢事件；ET 是优化方向，要处理"读空缓冲区"和饥饿问题。

**Q3：EventLoop 的跨线程唤醒是怎么实现的？**
每个 EventLoop 持有一个 eventfd。别的线程要往该 loop 投递任务时，先加锁入 pending 队列，再向 eventfd 写 1 个字节唤醒 epoll_wait；loop 线程被唤醒后执行 pending 任务。eventfd 是内核提供的"事件通知文件描述符"，比 pipe 少一次拷贝。

**Q4：Buffer 怎么解决粘包/半包？**
Buffer 用读/写两个游标管理字节流。TCP 是字节流，一次 readv 可能包含多个协议包（粘包）或只包含半个包（半包）。解决：协议头带 body 长度字段，解析时先检查"已读字节 >= 头长度 + body 长度"，不够就留在 Buffer 里等下一次读；够就按长度取走一条完整报文。Buffer 内部预留 prepend 空间，扩容时把未读数据搬到头部，避免频繁拷贝。

**Q5：Connection 的生命周期怎么管理？**
TcpConnection 用 `shared_ptr` 管理，存放在 TcpServer 的 map 里；Channel 回调捕获 `shared_from_this()` 防止回调执行中对象被销毁。连接关闭时先 disableAll 从 epoll 摘除，再回调 TcpServer 从 map 移除，最后一个引用释放时析构。

## 2. 线程池

**Q1：线程池为什么复用自有的 cpp_learn 项目而不是重新写？**
已有实现经过原项目测试，功能完整（fixed/cached、优先级队列、future、回调队列），直接复用省时且可控。我们做的改造：接入统一日志、修复 CreateThread 先 detach 后注册 id 的竞态（可能让析构死等）。

**Q2：任务队列用什么结构？如何保证优先级？**
全局 `priority_queue`（小顶堆，优先级值小者先出）+ 条件变量。提交时入堆，工作线程取堆顶。cached 模式动态扩线程：队列长度 > 空闲线程数且未达上限时新建线程，超时回收。

**Q3：submit 如何支持任意返回值？**
模板 + `packaged_task`：把可调用对象和参数打包成 `packaged_task<ReturnType()>`，入队执行，返回 `std::future`；异常通过 future 传播。队列满时 trySubmitFor 返回 nullopt 而不是无限阻塞。

**Q4：怎么优雅关闭线程池？**
置 `isRunning_ = false` 并 notify_all，工作线程在谓词（队列非空或停止）中醒来退出；用 `exitCond_` 等待全部线程退出后再析构，避免悬空访问。

## 3. 协议与 Codec

**Q1：自定义协议头包含哪些字段？为什么？**
24 字节：魔数(4) + 版本(1) + 类型(1) + 标志(2) + 序列号(8) + body 长度(4) + 保留(4)。魔数防脏数据，长度字段解决粘包/半包，序列号用于请求-响应匹配和超时重试去重，多字节字段统一大端。

**Q2：Codec 的半包处理为什么"解析但不消费"？**
`tryDecode` 先 peek 头部校验长度，数据不足返回 nullopt 且不移动游标；数据齐了才 retrieve 消费。这样底层只管"凑够一条完整报文"，粘包时循环解析多条，半包时自然等待。

**Q3：body 里为什么还要包一层 protobuf 信封？**
协议头只管传输帧，方法分发信息（`package.Service.Method`）和业务参数一起放 `RpcRequest` 信封，错误码/错误文本放 `RpcResponse` 信封。这样 Codec 与业务解耦，服务端用反射查注册表分发，错误统一走信封返回。

## 4. RPC 核心（RpcServer / RpcChannel / RpcController）

**Q1：服务端怎么分发请求？**
按 `method` 全名（如 `minirpc.example.CalculatorService.Calc`）取最后一个点切分 service 与 method；ServiceRegistry 按 service 全名存 protobuf `Service*`，再 `GetDescriptor()->FindMethodByName` 拿方法描述符，用 `GetRequestPrototype/GetResponsePrototype` 反射创建消息对象后 `CallMethod` 调用。新增服务只需 registerService，无需改框架。

**Q2：请求处理放在线程池，回复怎么回？**
请求在线程池线程执行，回包调用 `conn->send()`，内部通过 `EventLoop::runInLoop` 投递到 reactor 线程真正写 socket——天然线程安全，避免多个工作线程同时写一个 fd。

**Q3：同步客户端怎么等响应？超时重试怎么保证不混乱？**
RpcChannel 用阻塞 socket + poll 超时等待；每个请求带唯一 seq，响应按 seq 匹配。超时后重试会生成**新 seq** 重新发送，旧响应即使迟到也会因 seq 不匹配被丢弃，不会串到新请求。每次尝试前 Reset controller，避免上次超时的失败标记污染。重试只针对超时；连接关闭/系统错误/服务端明确报错不重试。

**Q4：RpcController 的作用？**
实现 protobuf 的 RpcController 接口：Failed/ErrorText/SetFailed/IsCanceled。扩展了带错误码的 SetFailed（kTimeout/kNoService/kNoMethod/kInvalidArgument 等）。服务方法里可主动 SetFailed 表示业务错误，框架自动把错误码+文本装进响应信封；客户端从 controller 读错误。

## 5. 注册中心

**Q1：注册中心为什么用 RPC 框架自己实现，而不是 etcd/Zookeeper？**
项目定位是"轻量、从零、可讲清"，且非目标明确排除全功能注册中心。用我们自己的 RpcServer 承载 RegistryService（Register/Discover/Heartbeat/Unregister），顺便验证框架通用性——注册中心本身只是一个普通 RPC 服务。

**Q2：心跳剔除怎么实现？**
节点启动时 Register（节点名 + host:port + 心跳超时），之后每 N ms 发一次 Heartbeat 刷新 `lastHeartbeat`；注册中心后台线程每 500ms 扫描一次，`now - lastHeartbeat > 超时` 的节点直接删除。客户端 Discover 只拿存活节点。

**Q3：优雅下线与崩溃有什么区别？**
优雅下线（shutdown）会调用 Unregister 立即移除；崩溃/宕机没有 Unregister，靠心跳超时被剔除——所以"故障剔除"的验收就是杀掉一个节点后等待超时，注册中心自动清理，客户端刷新后只剩存活节点。

## 6. 负载均衡

**Q1：轮询怎么实现？**
原子计数器 `index_++ % nodeCount`，按调用次序循环选节点。实现简单、均匀；缺点是不感知节点权重与状态，所以配合注册中心的存活列表使用。

**Q2：一致性哈希为什么用虚拟节点？**
直接哈希节点名，节点少时容易倾斜；每个节点在环上放 100 个虚拟点（`node#i` 哈希），分布更均匀。选路：哈希请求 key，在有序环上取第一个 >= 该哈希的虚拟点。同一 key 稳定落在同一节点，便于做粘性路由/缓存。

**Q3：一致性哈希"最小迁移"体现在哪？**
移除节点时，只有原本落在该节点弧段上的 key 会迁移到下一个节点，其他 key 不变（测试验证过该性质）；扩容同理。相比普通取模（节点数变化几乎全部 key 重新映射），迁移量小。

**Q4：客户端节点列表怎么保持新鲜？**
LoadBalanceChannel 缓存节点列表，带 TTL（默认 3s）按需刷新；调用失败如果是连接级错误（closed/system/timeout），会先刷新节点列表再换一个节点重试一次，实现故障转移。

## 7. 压测与性能

**Q1：压测怎么做的？数据怎么看？**
N 个客户端线程各持独立 RpcChannel 连续调用 Echo，逐次记录延迟（us），统计 QPS/p50/p90/p99/max；同时每 100ms 采样服务端 `/proc` 得到 CPU 与 RSS。结论：40 并发峰值 3.16 万 QPS，P99 <= 4.4ms（60 并发内），服务端仅占约 1.3 核。

**Q2：为什么吞吐上不去，瓶颈在哪？**
服务端 CPU 只用了 1.3 核，说明瓶颈不在 CPU 而在**单 Reactor**：每个响应都要经 eventfd 唤醒主循环再发送。提升路径：多 Reactor（每核一个 EventLoop）、批量唤醒、异步客户端。

**Q3：为什么 16 线程池反而比 4 线程慢？**
任务队列是全局互斥的优先级队列，工作线程越多，notify_all 后的锁竞争和惊群越明显；且回包仍要回主循环发送，业务线程多了并不能提高回包吞吐。实测 4 线程最优，这本身就是"先压测再优化"的例证。

## 8. 整体

**Q1：项目有哪些亮点？**
1) 从零实现 epoll Reactor + 线程池 + Protobuf RPC 全链路；2) 自定义 24 字节协议头，Codec 与业务解耦；3) 注册中心本身是框架上的一个 RPC 服务；4) 负载均衡（轮询/一致性哈希）+ 心跳剔除 + 故障转移；5) 完整压测报告（3 万+ QPS、P99 < 5ms）与决策记录。

**Q2：哪些地方可以继续做？**
多 Reactor、异步客户端（在途并发请求 + seq 匹配）、批量唤醒/合并写、多进程 + SO_REUSEPORT、注册中心持久化与 watch 推送、TLS。

