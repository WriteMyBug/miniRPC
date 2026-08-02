# MiniRPC

面向校招简历的轻量级 C++17 RPC 框架（开发中）。

基于 Linux epoll Reactor 模型、线程池与 Protobuf 序列化，从零实现服务注册/发现、负载均衡、超时重试与压测报告。详见 [计划书.md](计划书.md)。

## 开发环境（WSL2 内直开）

| 组件 | 版本 |
|---|---|
| 系统 | Ubuntu 24.04.4 LTS（WSL2，内核 6.6.87.2-microsoft-standard-WSL2） |
| 编译器 | g++ 13.3.0 |
| CMake | 3.28.3 |
| git | 2.43.0 |
| protobuf | protoc 3.21.12 + libprotobuf-dev（已安装，CMake 默认开启） |
| valgrind | 3.22.0（已安装） |

安装依赖：

```bash
sudo apt update
sudo apt install -y protobuf-compiler libprotobuf-dev valgrind
```

## 构建

```bash
cmake -B build
cmake --build build -j
```

## 运行 echo 示例

终端 1（服务端）：

```bash
./build/examples/echo_server 8888
```

终端 2（客户端，逐行回显）：

```bash
printf 'hello minirpc\nworld\n' | ./build/examples/echo_client 127.0.0.1 8888
```

也可以直接用 nc 验证：

```bash
nc 127.0.0.1 8888
```

并发压力验证（N 个客户端 × M 条消息，校验回显一致）：

```bash
./build/examples/echo_stress 127.0.0.1 8888 16 50
```

echo 服务端已接入线程池：主 reactor 负责 IO 与分发，消息处理在工作线程执行。

## RPC 示例与调用流程

终端 1（服务端，注册 EchoService 与 CalculatorService）：

```bash
./build/examples/calculator_server 8888
```

终端 2（客户端，同步调用）：

```bash
./build/examples/calculator_client 127.0.0.1 8888
```

一次完整 RPC 调用：

1. 客户端创建 `RpcChannel`（配置超时与重试次数），生成对应 `Service::Stub` 发起调用。
2. `RpcChannel::CallMethod` 组装内部信封 `RpcRequest`（method 全名 + 序列化请求），加上 24 字节协议头（type=request、唯一 seq）经 Codec 编码后发送；阻塞等待响应，超时自动重试（新 seq）。
3. 服务端 epoll 收到数据 → Codec 按长度字段拆包 → 提交线程池。
4. 工作线程解析 `RpcRequest` → 按 `service.method` 查注册表 → 反序列化请求 → 调用 Service 方法（传入 `RpcController`）。
5. 执行结果打包为 `RpcResponse` 信封 + 协议头（type=response、相同 seq）回发。
6. 客户端按 seq 匹配响应并反序列化填充；错误经 `RpcController`（错误码 + 错误文本）返回。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

覆盖：Buffer 读写与拆包、ThreadPool 提交/并发/异常传播/优先级/回调、Codec 编解码回环（逐字节半包、任意切分、粘包、非法报文）、RPC 集成（正常调用、服务端业务错误、超时重试、连接拒绝）。

> 注：`rpc_test` 需要创建 TCP socket，在受限沙箱中需放行网络或单独运行 `./build/tests/rpc_test`。

## 目录结构

```text
minRPC/
├── include/minirpc/
│   ├── common/    # 日志、错误码、工具
│   ├── net/       # EventLoop、EpollPoller、TcpServer、Buffer
│   ├── thread/    # ThreadPool（任务队列 + 条件变量，复用自 cpp_learn）
│   ├── codec/     # 协议头（24 字节）+ Protobuf 编解码
│   └── rpc/       # RpcServer、RpcChannel、RpcController、ServiceRegistry
├── src/
├── proto/         # echo / calculator / rpc 信封 proto（protoc 生成代码）
├── registry/      # 简版注册中心（第 4 周）
├── examples/echo/ # echo 示例
├── examples/rpc/  # RPC calculator/echo 示例
├── benchmark/     # 压测程序（第 4 周）
└── tests/         # 单元测试
```

## 当前进度

- [x] WSL2 环境验证
- [x] CMake 工程 + 目录骨架
- [x] 日志模块
- [x] EventLoop + EpollPoller
- [x] Buffer（读写缓冲、粘包半包处理）
- [x] TcpServer / TcpConnection 连接管理
- [x] echo 示例（nc / 自写客户端均可回显）
- [x] 安装 protobuf / valgrind（用户 sudo 安装，CMake 接入验证通过）
- [x] ThreadPool（复用 cpp_learn 线程池，接入统一日志）
- [x] 协议头（魔数/版本/类型/序列号/长度）+ Codec 编解码
- [x] protoc 接入 CMake（Echo/Calculator proto）
- [x] codec_test / threadpool_test（粘包半包边界、并发与优先级）
- [x] echo 并发验证（16 客户端 × 50 消息 = 800 请求，0 失败）
- [x] ServiceRegistry / RpcServer / RpcChannel / RpcController
- [x] 内部信封 proto（RpcRequest/RpcResponse）+ 完整调用链路
- [x] 超时重试（黑洞服务端验证：2 次尝试后 kTimeout）
- [x] rpc_test 集成测试 + calculator 示例演示通过
