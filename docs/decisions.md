# 设计决策记录

按开发协作约定第 4 条：遇到不确定的设计先记录到 docs/decisions.md 再实现。

## D1（2026-08-02）线程池复用策略

- 决策：直接复用 `/home/nina/cpp_learn/threadpool`（用户此前完成的项目），移植进 minirpc，不重新实现。
- 理由：现有实现功能完整（fixed/cached 模式、优先级队列、future、回调队列）且经过原项目测试；计划书中 thread 模块预估 0-300 行正是基于"复用"。
- 改造点：
  1. 移植到 `minirpc` 命名空间；
  2. 日志接入 minirpc Logger（原实现输出到 std::cout）；
  3. 修复 `CreateThread` 竞态：原实现先 detach 再注册 thread id，线程若提前退出会留下脏 id，析构可能死等；改为先注册 id 再 detach。
- 影响：维护成本低；面试可讲"为什么不复用 asio 但复用自有线程池 + 做了什么改造"。

## D2（2026-08-02）自定义协议头

- 决策：固定 24 字节头部：魔数(4) + 版本(1) + 类型(1) + 标志(2) + 序列号(8) + body 长度(4) + 保留(4)，多字节字段统一大端。
- 理由：TLV 思路，长度字段天然解决粘包/半包；序列号供第 3 周超时重试去重；魔数/版本/长度上限防脏数据与恶意报文。
- 影响：Codec 与业务协议解耦，body 直接承载 protobuf 序列化字节；RpcChannel/RpcServer 只需复用同一 Codec。

## D3（2026-08-02）RPC 内部信封

- 决策：协议头 body 内再包一层 protobuf 信封 `RpcRequest { method, request }` / `RpcResponse { error_code, error_message, response }`（proto/rpc.proto）。
- 理由：method 分发信息与业务参数共用同一序列化体系；错误码/错误文本与成功数据统一走响应信封，客户端无需额外协议。
- 影响：Codec 保持通用（只管 24 字节头 + body），RPC 层负责信封编解码与 Service 分发。

## D4（2026-08-02）同步客户端通道设计

- 决策：`RpcChannel` 使用阻塞 socket + poll 等待，同一通道的调用以互斥锁串行化；不做独立接收线程。
- 理由：第 3 周目标是同步调用，串行 + 超时轮询实现最简单可靠；每个请求带唯一 seq，重试用新 seq 避免与旧响应混淆。
- 影响：并发调用会被串行化（可接受）；第 4 周异步客户端（并发/回调）再引入独立接收线程或每连接事件循环。
- 细节：重试仅针对超时（连接关闭/系统错误/服务端明确报错不重试）；每次尝试前 Reset controller，避免上次超时残留失败标记。

## D5（2026-08-02）注册中心即 RPC 服务

- 决策：注册中心不做独立网络协议，直接复用本框架：一个 `RpcServer` 承载 `RegistryService`（Register/Discover/Heartbeat/Unregister），节点与客户端通过 `RpcChannel` 调用它。
- 理由：范围受控（非目标排除 etcd/Zookeeper），同时验证框架通用性——"注册中心只是框架上的一个普通服务"是很好的面试话术。
- 心跳剔除：节点启动 Register 后每 N ms Heartbeat 刷新时间戳；注册中心后台线程每 500ms 扫描，超时（默认 6s）即删除；Discover 只返回存活节点。

## D6（2026-08-02）负载均衡通道设计

- 决策：`LoadBalanceChannel`（google::protobuf::RpcChannel 子类）内部缓存节点列表（TTL 刷新），按策略选节点，并为每个节点维护一个 `RpcChannel`；连接级失败（closed/system/timeout）先刷新列表再换节点重试一次，业务错误不重试。
- 轮询：原子计数取模；一致性哈希：虚拟节点（每节点 100 点）+ 有序环 + std::hash（实测分布均匀；FNV-1a 对短串雪崩差被弃用）。
- 代价：与同步通道一致，通道级串行（互斥锁保护），并发调用会被串行化——异步客户端（在途并发 + seq 匹配）作为可选加分项暂缓。

## D7（2026-08-02）压测发现与调优结论

- 4 线程池最优：16/8 线程反而更慢（全局队列锁竞争 + notify_all 惊群），实测数据见 benchmark/report.md。
- 瓶颈在单 Reactor：服务端 CPU 仅约 1.3 核，吞吐峰值 ~3.16 万 QPS；每个响应都要 eventfd 唤醒主循环。
- 目标达成：Release 下 40 并发 31587 QPS、60 并发内 P99 <= 4.4ms；继续提升走多 Reactor / 批量唤醒 / 异步客户端。

## D8（2026-09-01）多 Reactor 升级

- 决策：新增 `EventLoopPool`（N 个 EventLoop 各跑一个线程），`TcpServer` 按轮询把新连接分发给子循环，`RpcServer` 增加 `ioThreads` 参数；ioThreads=0 保持单 Reactor 行为。
- 理由：单 Reactor 压测 CPU 仅 1.26 核、200 并发吞吐封顶约 2.3 万 QPS，瓶颈是"所有连接 IO 串在一个事件循环"；多 Reactor 把读/写/唤醒分摊到多核。
- 关键实现点：
  1. EventLoop 必须在创建它的线程内运行 loop()，因此每个 loop 在线程内构造（promise 传递指针）；
  2. 连接表 `connections_` 加互斥锁（主循环插入、子循环删除）；
  3. 连接移除必须路由回连接所属的子循环（`conn->loop()->runInLoop`），修复了"在主循环断言"的跨线程 bug；
  4. 析构顺序：先销毁 TcpServer（连接释放时子循环仍存活），再销毁 EventLoopPool。
- 结果（Release，同机同法）：200 并发 QPS 2.25 万 → 约 8 万（约 3.5 倍）；CPU 占用 1.26 → 4.18 核（约 3.3 倍）；200 并发 P99 61ms → 6.5ms。详见 benchmark/report.md 第 6 节。
- 代价与后续：连接分发不感知各循环负载（可做动态迁移）；批量唤醒/合并发送、异步客户端、SO_REUSEPORT 多进程仍为后续方向。
