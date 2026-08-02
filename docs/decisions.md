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
