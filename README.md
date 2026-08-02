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
| protobuf | 待安装（第 2 周 codec 模块使用） |

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

## 测试

```bash
ctest --test-dir build --output-on-failure
```

## 目录结构

```text
minRPC/
├── include/minirpc/
│   ├── common/    # 日志、错误码、工具
│   ├── net/       # EventLoop、EpollPoller、TcpServer、Buffer
│   ├── thread/    # ThreadPool（第 2 周）
│   ├── codec/     # 协议头、Protobuf 编解码（第 2 周）
│   └── rpc/       # RpcServer、RpcChannel（第 3 周）
├── src/
├── proto/         # .proto 定义（第 2 周）
├── registry/      # 简版注册中心（第 4 周）
├── examples/echo/ # echo 示例
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
- [ ] 安装 protobuf（需要 sudo，等待用户执行）

