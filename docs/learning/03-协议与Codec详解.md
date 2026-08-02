# 阶段 3：协议与 Codec 详解（2.5h）

对应代码：`include/minirpc/codec/`、`src/codec/`、`proto/`。

## 1. 学习目标

- 能默写 24 字节协议头的布局并解释每个字段；
- 能讲清 `tryDecode` 为什么"半包返回 nullopt 且不消费数据"；
- 能讲清粘包时如何连续解析出多条报文。

## 2. 前置知识

- 字节序：大端（网络序）vs 小端（x86 本机序）；
- protobuf 基本用法（message 的 set/get、SerializeToString/ParseFromString）；
- 阶段 1 的 Buffer 知识。

## 3. 架构设计详解

### 3.1 分层：传输帧 vs 业务信封

```text
一条 TCP 报文 = 协议头(24B) + body
                              └─ body 内部又是一个 protobuf 信封
                                 RpcRequest { method 全名, 序列化请求 }
                                 RpcResponse{ error_code, error_message, 序列化响应 }
```

为什么分两层？
- 协议头管"传输帧"：长度、序号、类型——和业务无关；
- 信封管"RPC 语义"：方法分发、错误码——和传输无关；
- 这样 Codec 保持通用，将来换业务协议（比如换成 JSON）不用动网络层。

### 3.2 24 字节协议头布局（Protocol.h）

```text
偏移  长度  字段        说明
0     4     magic      魔数 0x4D494E52（"MINR"），防脏数据
4     1     version    协议版本 = 1
5     1     type       1=请求 2=响应
6     2     flags      预留标志位
8     8     seq        序列号：请求-响应匹配、超时重试去重
16    4     bodyLen    body 字节数（protobuf 信封）
20    4     reserved   保留
--------------------------------------------
合计   24 字节（static_assert 保证）
```

多字节字段（magic/seq/bodyLen/flags/reserved）传输一律**大端**，因为网络协议约定俗成，且跨机器可移植。

## 4. 重点实现拆解

### 4.1 编码 encode（src/codec/Codec.cpp）

```text
appendU32(magic) → 1B version → 1B type → appendU16(flags)
→ appendU64(seq) → appendU32(bodyLen) → appendU32(reserved) → body 原文
```

`appendU32` 本质：`htonl(v)` 后把 4 字节塞进 Buffer——把本机小端转成大端。

### 4.2 半包解析 tryDecode（核心，务必吃透）

```cpp
if (readableBytes < 24)             return nullopt;   // 头都没齐
校验 magic / version / bodyLen 上限
if (readableBytes < 24 + bodyLen)   return nullopt;   // 头齐 body 没齐
in->retrieve(24);                                     // 消费头
body = in->retrieveAsString(bodyLen);                 // 消费 body
return 报文;
```

关键设计：**前两步只看不拿（peek）**，数据不够就返回空，一个字节都不消费；数据齐了才 retrieve。这样：

```text
半个包来了 → 返回 nullopt，字节留在 Buffer → 下次读到剩余部分 → 再解析成功
两个包一起来 → 循环 tryDecode，一次消费一条 → 第二条继续留在 Buffer
```

### 4.3 防御（面试常问"如果对端发脏数据"）

- 魔数不对 → 抛异常（服务端会断开连接）；
- version 不认 → 抛异常；
- bodyLen 超 64MB → 抛异常（防恶意长度导致内存暴涨）。

### 4.4 信封 proto（proto/rpc.proto）

```proto
message RpcRequest  { string method = 1; bytes request = 2; }
message RpcResponse { int32 error_code = 1; string error_message = 2;
                      bytes response = 3; }
```

错误码/错误文本和成功数据**都走同一个信封**，客户端解析时先看 error_code，再决定填 response 还是报错——不需要第二套协议。

## 5. 讲给自己听

1. 为什么长度字段必须放在头部，而不是等收齐再猜？
2. tryDecode 在"头齐 body 未齐"时返回 nullopt，Buffer 的游标动没动？
3. 粘包时 while(tryDecode) 循环会不会死循环？什么情况下会？
4. 为什么 seq 是 8 字节？重试时换新 seq 的意义？
5. 大端转换做错会有什么现象（提示：换台机器测）？

## 6. 小练习

- 练习 A（必做，30min）：读 `tests/codec_test.cpp` 的 6 个用例，逐个注释说明在测什么；然后删掉其中一个用例的断言，观察失败信息。
- 练习 B（必做，30min）：把 bodyLen 上限临时改成 10 字节，编一个 20 字节 body 的报文跑 codec_test，确认抛异常；改回来。
- 练习 C（选做）：给协议头 flags 加"第 0 位 = 压缩"语义，在 encode/tryDecode 里读写出 flags，写一个断言验证。

## 7. 源码阅读清单（阶段 3 对照看）

| 知识点 | 打开文件 | 重点函数/位置 | 看什么 |
|---|---|---|---|
| 协议头布局 | include/minirpc/codec/Protocol.h | `struct ProtocolHeader` 与常量区 | 24 字节逐字段注释；static_assert 保证大小；kMaxBodySize 防恶意长度 |
| 大端编码 | src/codec/Codec.cpp | `appendU16/U32/U64` | htonl/htons/htobe64 的用法；编码顺序与头部布局一一对应 |
| 半包解码 | src/codec/Codec.cpp | `tryDecode()` | 三段式：头没齐→nullopt；头齐 body 没齐→nullopt；齐了才 retrieve |
| 信封协议 | proto/rpc.proto | RpcRequest / RpcResponse | 为什么 body 里还要包一层（method 分发 + 错误码） |
| 粘包消费 | tests/codec_test.cpp | 用例 3/4（粘包、任意切分） | 观察 while(tryDecode) 一条条消费的行为 |

